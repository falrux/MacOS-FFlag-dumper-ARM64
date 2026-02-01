#include <iostream>
// #include <vector>
#include <chrono>
#include <string>
#include <cctype>
#include <map>
#include <regex>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <capstone/capstone.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <curl/curl.h>

using namespace std;

// globals
uint8_t* g_Data = nullptr;
size_t g_Size = 0;
uint64_t g_TextVmAddr = 0, g_TextFileOff = 0, g_TextSize = 0;

// functions
size_t writeCB(void* ptr, size_t size, size_t nmemb, string* data) {
    data->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

string GetClientVersion() { // asumes latest version, doesnt get the binary version, could probably fix that but this was easier
    auto* curl = curl_easy_init();
    string resp, ver = "waddo";

    if (!curl) return ver;
    curl_easy_setopt(curl, CURLOPT_URL, "https://clientsettingscdn.roblox.com/v2/client-version/MacPlayer");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCB);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "david/baszucki"); // unpatchable (+vouch)
    
    if (curl_easy_perform(curl) == CURLE_OK) {
        auto pos = resp.find("\"clientVersionUpload\"");
        if (pos != string::npos) {
            auto start = resp.find("version-", pos);
            if (start != string::npos) {
                auto end = resp.find("\"", start);
                if (end != string::npos) ver = resp.substr(start, end - start);
            }
        }
    }
    curl_easy_cleanup(curl);
    return ver;
}

const char* PtrFromAddr(uint64_t addr) {
    return (const char*)(g_Data + (addr - g_TextVmAddr + g_TextFileOff));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Failed >> usage is %s <roblox_binary_path>\n", argv[0]);
        return 1;
    }

    auto start = chrono::high_resolution_clock::now(); // timer

    string version = GetClientVersion();

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) return 1;
    struct stat st;
    fstat(fd, &st);
    g_Size = st.st_size;
    g_Data = (uint8_t*)mmap(nullptr, g_Size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    // finds the __TEXT segment of roblox
    auto* header = (mach_header_64*)g_Data;
    auto* cmd = (uint8_t*)(header + 1);
    
    for (int i = 0; i < header->ncmds; i++) {
        auto* lc = (load_command*)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            auto* seg = (segment_command_64*)lc;
            if (strcmp(seg->segname, "__TEXT") == 0) {
                g_TextVmAddr = seg->vmaddr;
                g_TextFileOff = seg->fileoff;
                g_TextSize = seg->filesize;
                break;
            }
        }
        cmd += lc->cmdsize;
    }

    csh cs;
    cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &cs);
    cs_option(cs, CS_OPT_DETAIL, CS_OPT_ON);

    const char* fflagString = "PktDropStatsReportThreshold"; // random FFlag string to base the search off
    uint64_t anchorAddr = 0; // fflagstring address
    
    void* found = memmem(g_Data, g_Size, fflagString, strlen(fflagString));
    if (!found) {
        printf("Failed >> Couldnt find fflag string.\n");
        return 1;
    }
    
    uint64_t foundOffset = (uint8_t*)found - g_Data;

    // get vm address for this string
    cmd = (uint8_t*)(header + 1);
    for (int i = 0; i < header->ncmds; i++, cmd += ((load_command*)cmd)->cmdsize) {
        auto* seg = (segment_command_64*)cmd;
        if (seg->cmd == LC_SEGMENT_64 &&
            foundOffset >= seg->fileoff &&
            foundOffset < (seg->fileoff + seg->filesize)) {
            anchorAddr = seg->vmaddr + (foundOffset - seg->fileoff);
            break;
        }
    }

    // find the function that registers all fflags
    // adrp + add that reso;ves to the string address
    uint64_t registerFuncAddr = 0;
    uint8_t* textStart = g_Data + g_TextFileOff;
    
    for (size_t i = 0; i < g_TextSize - 8; i++) {
        // look for adrp instruction
        uint32_t instr1 = *(uint32_t*)(textStart + i);
        if ((instr1 & 0x9F000000) != 0x90000000) continue;
        
        // next instr should be add
        if (i + 4 >= g_TextSize) continue;
        uint32_t instr2 = *(uint32_t*)(textStart + i + 4);
        if ((instr2 & 0xFFC00000) != 0x91000000) continue;
        
        // resolve address loaded by adrp + add
        uint64_t adrp_imm = ((instr1 >> 29) & 3) | ((instr1 >> 3) & 0x1FFFFC);
        if (adrp_imm & 0x20000) adrp_imm |= 0xFFFC0000;
        uint64_t page_addr = (g_TextVmAddr + i) & ~0xFFF;
        uint64_t adrp_target = page_addr + (adrp_imm << 12);
        
        uint64_t add_imm = (instr2 >> 10) & 0xFFF;
        uint64_t final_target = adrp_target + add_imm;
        
        if (final_target != anchorAddr) continue;
        
        // look for a branch/call after the string loads
        cs_insn* insn;
        size_t count = cs_disasm(cs, textStart + i + 8, 32, g_TextVmAddr + i + 8, 0, &insn); // dissasembles a little bit foward from the lea to find a call or jmp following it
        for (size_t j = 0; j < count; j++) {
            if (insn[j].id == ARM64_INS_BL || insn[j].id == ARM64_INS_B) {
                if (insn[j].detail->arm64.operands[0].type == ARM64_OP_IMM) {
                    registerFuncAddr = insn[j].detail->arm64.operands[0].imm;
                    break;
                }
            }
        }
        cs_free(insn, count);
        if (registerFuncAddr) break;
    }

    if (!registerFuncAddr) {
        printf("Failed >> Could not find register function.\n");
        return 1;
    }

    // scan for direct call/jmp references to the register function
    map<string, uint64_t> results;
    
    //  scan 1 byte at a time for E8/E9 (call/jmp)
    for (size_t i = 0; i < g_TextSize - 4; i++) {
        uint32_t instr = *(uint32_t*)(textStart + i);
        
        uint64_t target = 0;
        if ((instr & 0xFC000000) == 0x94000000) {
            int32_t imm26 = (instr & 0x03FFFFFF);
            if (imm26 & 0x02000000) imm26 |= 0xFC000000;
            target = (g_TextVmAddr + i) + (imm26 << 2);
        } else if ((instr & 0xFC000000) == 0x14000000) { 
            int32_t imm26 = (instr & 0x03FFFFFF);
            if (imm26 & 0x02000000) imm26 |= 0xFC000000;
            target = (g_TextVmAddr + i) + (imm26 << 2);
        } else {
            continue;
        }

        if (target != registerFuncAddr) continue;

        // registerfflag("PktDropStatsReportThreshold", &dword_1058BC19C, 2LL); 
        // we check the RDI (first argument) for the name string and then RSI (second argument) for the fflag address
        
        // x0: fflag name
        // x1: fflag var address
        uint64_t currentAddr = g_TextVmAddr + i;
        uint64_t lookBackAddr = (currentAddr > 40) ? currentAddr - 40 : g_TextVmAddr;
        
        cs_insn* insn;
        size_t count = cs_disasm(cs, textStart + (lookBackAddr - g_TextVmAddr), 
                                    currentAddr - lookBackAddr, lookBackAddr, 0, &insn);
        
        uint64_t nameAddr = 0, varAddr = 0;

        if (count > 0) {
            for (size_t j = 0; j < count; j++) {
                if (insn[j].id == ARM64_INS_ADRP) {
                    if (j + 1 < count && insn[j+1].id == ARM64_INS_ADD) {
                        uint32_t adrp_instr = *(uint32_t*)(textStart + (insn[j].address - g_TextVmAddr));
                        uint32_t add_instr  = *(uint32_t*)(textStart + (insn[j+1].address - g_TextVmAddr));
                        
                        uint64_t adrp_imm = ((adrp_instr >> 29) & 3) | ((adrp_instr >> 3) & 0x1FFFFC);
                        if (adrp_imm & 0x20000) adrp_imm |= 0xFFFC0000;
                        uint64_t page_addr = insn[j].address & ~0xFFF;
                        uint64_t adrp_target = page_addr + (adrp_imm << 12);
                        
                        uint64_t add_imm = (add_instr >> 10) & 0xFFF;
                        uint64_t final_target = adrp_target + add_imm;
                        
                        uint8_t dst_reg = (add_instr >> 0) & 0x1F;
                        if (dst_reg == 0) nameAddr = final_target;
                        if (dst_reg == 1) varAddr  = final_target;
                    }
                }
            }
            cs_free(insn, count);
        }

        if (nameAddr && varAddr) {
            const char* namePtr = PtrFromAddr(nameAddr);
            if (namePtr && strlen(namePtr) > 2) {
                results[namePtr] = varAddr;
                // fprintf(stderr, "Found FFlag '%s' nameAddr=0x%llx varAddr=0x%llx\n", namePtr, (unsigned long long)nameAddr, (unsigned long long)varAddr);
            }
        }
    }

    auto end = chrono::high_resolution_clock::now();
    auto ms = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    // we can just use cout because all output is directed to the .hpp file (assuming it was run correctly)
    cout << "// FFlag dumper by waddotron - (" << version << ")" << endl;
    cout << "// Native arm64 support by falrux" << endl;
    cout << "// FFlags dumped - " << results.size() << endl;
    cout << "// Dumped in - " << ms << "ms" << endl << endl; // if this is more than 1000 ms... then i'll kms. i ain't giving money for my shitty native arm64 support fork :pray:

    for (auto const& [name, addr] : results) {
        cout << "constexpr uintptr_t " << name << " = 0x" << hex << addr << ";" << endl;
    }

    cs_close(&cs);
    munmap(g_Data, g_Size);
    return 0;
}

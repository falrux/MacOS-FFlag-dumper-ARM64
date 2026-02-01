# Waddotrons FFlag dumper
**(With native ARMA4/Silicon Support)**

### Simplified Static MacOS Roblox FFlag dumper, for usage with Silicon roblox builds.
`this is ONLY for arm64/siicon, for intel go to the original repo`

### - I was originally making an fflags dumper for arm64 myself until somebody told me this repo came out, so i decided to help out and add native arm64 support from my version

## Prerequisites (ARM64/SILICON ONLY)

**1. Install Command Line Tools:**
```bash
xcode-select --install
```

**2. Install Dependencies:**
```bash
brew install capstone curl
```

---

**Building:**

```bash
clang++ -O3 -std=c++17 -target arm64-apple-darwin main.cpp -o dumpfflags -I/opt/homebrew/include -L/opt/homebrew/lib -lcapstone -lcurl
```

# Usage
*Args: roblox_binary_path > output_file*
```bash
./dumpfflags /Applications/Roblox.app/Contents/MacOS/RobloxPlayer > fflags.hpp
```
*The output file is created automatically.*

---
- If you need help or any issues accour, contact me on discord: (DSC: **falrux**)
- Credits to the original creator, he's a very cool guy (DSC: **physics1514_**)

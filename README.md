# Waddotrons fflag dumper
### Simplified MacOS Roblox FFlag dumper, for usage with Intel roblox builds.
`>> supports both intel and arm64 macs <<`

## Prerequisits:
**ARM64 / Silicon:**
```
arch -x86_64 /usr/local/bin/brew install capstone curl
```
**Intel:**
```
brew install capstone curl
```
If you dont have homebrew, or compiler tools use the following before anything else:
```
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```
```
xcode-select --install
```
# Building / Usage
## Build arm64 / silicon:
```
arch -x86_64 clang++ -O3 -std=c++17 -target x86_64-apple-darwin \main.cpp -o dumpfflags -L/usr/local/lib -lcapstone -lcurl
```

## Build Intel:
```
clang++ -O3 -std=c++17 -target x86_64-apple-darwin \main.cpp -o dumpfflags -L/usr/local/lib -lcapstone -lcurl
```
## Universal run:
args: roblox_binary_path > output_file
```
./dumpfflags /Applications/Roblox.app/Contents/MacOS/RobloxPlayer > fflags.hpp
```
*The output file is created automatically*

Any problems / errors lmk on discord **physics1514_**

# Installation

## Prerequisites

### Linux

```bash
# Debian / Ubuntu
sudo apt install cmake g++ libstdc++-dev

# Arch Linux
sudo pacman -S cmake gcc

# Fedora
sudo dnf install cmake gcc-c++
```

No additional runtime dependencies are required. All data is collected from kernel pseudo-filesystems (`/proc`, `/sys`) and POSIX APIs.

### macOS

```bash
# Xcode Command Line Tools (provides clang++ and cmake via Homebrew)
xcode-select --install
brew install cmake
```

> **Note:** Temperature readings on macOS require IOKit linkage (bundled automatically when using the CMake build).

---

## Building from Source

### CMake (recommended)

```bash
git clone https://github.com/yourname/sysmon.git
cd sysmon
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/sysmon
```

### Install system-wide

```bash
cmake --install build --prefix /usr/local
sysmon          # now in PATH
```

### Single-file g++ compilation (Linux)

```bash
g++ -std=c++20 -O2 -I include \
    src/*.cpp \
    -o sysmon
```

### Single-file clang++ compilation (macOS)

```bash
clang++ -std=c++20 -O2 -I include \
    src/*.cpp \
    -framework IOKit -framework CoreFoundation \
    -o sysmon
```

---

## Running Tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

---

## Package Managers (planned)

| Platform | Command |
|----------|---------|
| Arch AUR | `yay -S sysmon` |
| Homebrew | `brew install sysmon` |
| apt      | planned |

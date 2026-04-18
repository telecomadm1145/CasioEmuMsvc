#!/bin/bash

set -e

INFO='\033[1;34m'
SUCCESS='\033[1;32m'
WARNING='\033[1;33m'
ERROR='\033[1;31m'
NC='\033[0m'
AUTO_YES=0

for arg in "$@"; do
    case $arg in
        -y|--yes)
            AUTO_YES=1
            shift
            ;;
        *)
            ;;
    esac
done

if [ "$(id -u)" -eq 0 ]; then
  PKG_MANAGER="apt-get"
else
  if command -v sudo &> /dev/null; then
    PKG_MANAGER="sudo apt-get"
  else
    echo -e "${ERROR}Error: This script needs root or sudo privileges to install the required packages.${NC}"
    exit 1
  fi
fi

REQUIRED_PACKAGES=(
    build-essential
    cmake
    clang
    ninja-build
    git
    libx11-dev
    libxext-dev
    libgl1-mesa-dev
    libcurl4-openssl-dev
    zlib1g-dev
)

PACKAGES_TO_INSTALL=()

echo -e "${INFO}---> Check dependencies...${NC}"
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if dpkg -s "$pkg" &> /dev/null; then
        echo -e "  [${SUCCESS}✓${NC}] Found: $pkg"
    else
        echo -e "  [${WARNING}✗${NC}] Missing:       $pkg"
        PACKAGES_TO_INSTALL+=("$pkg")
    fi
done

if [ ${#PACKAGES_TO_INSTALL[@]} -ne 0 ]; then
    echo ""
    echo -e "${WARNING}Some required packages are not installed.${NC}"
    if [ $AUTO_YES -eq 1 ]; then
        echo -e "${WARNING}Automatic installation triggered by -y/--yes. Proceeding...${NC}"
        confirm="y"
    else
        read -p "Do you want to install them now?? (y/n): " confirm
    fi
    
    if [[ "$confirm" == [yY] || "$confirm" == [yY][eE][sS] ]]; then
        echo -e "${INFO}---> Update package list...${NC}"
        $PKG_MANAGER update
        echo -e "${INFO}---> Start installing missing packages...${NC}"
        $PKG_MANAGER install -y "${PACKAGES_TO_INSTALL[@]}"
        echo -e "${SUCCESS}---> Installation complete!${NC}"
    else
        echo -e "${ERROR}Aborted. Cannot continue build without sufficient dependencies.${NC}"
        exit 1
    fi
fi
echo ""

echo -e "${INFO}---> Clean up old build folder...${NC}"
if [ -d "build" ]; then
    rm -rf build
fi
mkdir build

echo -e "${INFO}---> Start configuring CMake for static builds...${NC}"
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DBUILD_EXECUTABLE=ON

if [ $? -ne 0 ]; then
    echo -e "${ERROR}Error: CMake configuration failed.${NC}"
    exit 1
fi

echo -e "${SUCCESS}---> CMake configuration successful!${NC}"
echo ""

echo -e "${INFO}---> Start compiling the project (may take a few minutes)...${NC}"
cmake --build build -- -j$(nproc)

if [ $? -ne 0 ]; then
    echo -e "${ERROR}Error: Compilation failed.${NC}"
    exit 1
fi
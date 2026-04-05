#include <iostream>
#include <string>
#include "RomPackage.h"

#ifdef main
#undef main
#endif

void printUsage() {
    std::cout
        << "Usage: RomPacker.exe <command> <file> <directory> [password]\n"
        << "Commands:\n"
        << "  p, pack    : Pack a ROM directory into a file\n"
        << "  u, unpack  : Unpack a ROM file into a directory\n"
        << "Options:\n"
        << "  <file>     : Path to the ROM package file\n"
        << "  <directory>: Path to the ROM directory\n"
        << "  [password] : Optional password for encryption/decryption\n";
}

int main(int argc, const char** argv) {
    if (argc < 4 || argc > 5) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    std::string filePath = argv[2];
    std::string directoryPath = argv[3];
    std::string password = (argc == 5) ? argv[4] : "";

    RomPackage package;

    try {
        if (command == "p" || command == "pack") {
            package.Load(directoryPath);
            if (!password.empty()) {
                package.Encrypt(password);
            }
            WriteFile(filePath, package);
            std::cout << "ROM package created successfully.\n";
        }
        else if (command == "u" || command == "unpack") {
            ReadFile(filePath, package);
            if (package.IsEncrypted) {
                if (password.empty()) {
                    throw std::runtime_error("Password required for decryption.");
                }
                package.Decrypt(password);
            }
            package.ExtractTo(directoryPath);
            std::cout << "ROM package extracted successfully.\n";
        }
        else if (command == "extract_rom") {
            ReadFile(filePath, package);
            WriteFile("rom.bin", package.RomData);
            WriteFile("flash.bin", package.FlashData);
            WriteFile("interface.bin", package.InterfaceData);
        }
        else {
            throw std::runtime_error("Invalid command. Use 'p' for pack or 'u' for unpack.");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
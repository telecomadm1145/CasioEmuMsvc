#pragma once
#include <vector>
#include <string>
#include <ctime>
#include <iostream>
#include <cstdint>

namespace casioemu {
    class Chipset;

    struct SnapshotNode {
        std::vector<uint8_t> state_data;
        std::vector<uint8_t> thumbnail;
        std::string description;
        std::time_t timestamp;
        std::vector<SnapshotNode*> children;
        SnapshotNode* parent = nullptr;

        SnapshotNode();
        ~SnapshotNode();

        // Recursively serialize to file
        void Write(std::ostream& os);
        void Read(std::istream& is);
    };

    class SnapshotManager {
    public:
        SnapshotNode* root = nullptr;
        SnapshotNode* current = nullptr;

        SnapshotManager();
        ~SnapshotManager();

        void TakeSnapshot(Chipset& chipset, const std::string& desc, int width, int height, const void* pixels);
        void LoadSnapshot(Chipset& chipset, SnapshotNode* node);
        void DeleteSnapshot(SnapshotNode* node);

        void SaveToFile(const std::string& path);
        void LoadFromFile(const std::string& path);

        // Helper to clear the tree
        void Clear();
    };
}

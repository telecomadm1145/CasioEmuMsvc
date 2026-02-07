#include "Snapshot.hpp"
#include "Binary.h"
#include "Chipset/Chipset.hpp"
#include <sstream>
#include <cstring>
#include <algorithm>

namespace casioemu {

    SnapshotNode::SnapshotNode() {
        timestamp = std::time(nullptr);
    }

    SnapshotNode::~SnapshotNode() {
        for(auto c : children) delete c;
    }

    void SnapshotNode::Write(std::ostream& os) {
        Binary::Write(os, state_data);
        Binary::Write(os, thumbnail);
        Binary::Write(os, description);
        Binary::Write(os, (uint64_t)timestamp);

        uint32_t child_count = (uint32_t)children.size();
        Binary::Write(os, child_count);
        for(auto c : children) {
            c->Write(os);
        }
    }

    void SnapshotNode::Read(std::istream& is) {
        Binary::Read(is, state_data);
        Binary::Read(is, thumbnail);
        Binary::Read(is, description);
        uint64_t ts;
        Binary::Read(is, ts);
        timestamp = (std::time_t)ts;

        uint32_t child_count;
        Binary::Read(is, child_count);
        children.resize(child_count);
        for(uint32_t i=0; i<child_count; ++i) {
            children[i] = new SnapshotNode();
            children[i]->parent = this;
            children[i]->Read(is);
        }
    }

    SnapshotManager::SnapshotManager() {}
    SnapshotManager::~SnapshotManager() { Clear(); }

    void SnapshotManager::Clear() {
        if(root) delete root;
        root = nullptr;
        current = nullptr;
    }

    void SnapshotManager::TakeSnapshot(Chipset& chipset, const std::string& desc, int width, int height, const void* pixels) {
        SnapshotNode* node = new SnapshotNode();
        node->description = desc;
        node->parent = current;

        // Serialize State
        std::stringstream ss;
        chipset.SaveState(ss);
        std::string s = ss.str();
        node->state_data.assign(s.begin(), s.end());

        // Thumbnail
        // Format: [Width(4)][Height(4)][Pixels...]
        size_t pixel_size = width * height * 4;
        node->thumbnail.resize(sizeof(int)*2 + pixel_size);
        int* hdr = (int*)node->thumbnail.data();
        hdr[0] = width;
        hdr[1] = height;
        memcpy(node->thumbnail.data() + sizeof(int)*2, pixels, pixel_size);

        if(current) {
            current->children.push_back(node);
        } else {
            if (root) delete root; // Should only happen if we reset
            root = node;
        }
        current = node;
    }

    void SnapshotManager::LoadSnapshot(Chipset& chipset, SnapshotNode* node) {
        if(!node) return;

        // Deserialize State
        if(node->state_data.empty()) return;

        // stringstream is read-only if initialized with string
        std::string s(node->state_data.begin(), node->state_data.end());
        std::stringstream ss(s);
        chipset.LoadState(ss);

        current = node;
    }

    void SnapshotManager::DeleteSnapshot(SnapshotNode* node) {
        if (!node) return;

        // Check if current needs update
        SnapshotNode* p = current;
        bool current_will_die = false;
        while(p) {
            if(p == node) {
                current_will_die = true;
                break;
            }
            p = p->parent;
        }

        if (current_will_die) {
            current = node->parent;
        }

        if (node == root) {
            // If deleting root, everything goes.
            // But we already handled current.
            // Be careful: Clear() deletes root.
            // We should just detach children or delete them?
            // Deleting root implies deleting the whole tree.
            // So Clear() is correct.
            Clear();
            return;
        }

        if (node->parent) {
            auto& siblings = node->parent->children;
            auto it = std::find(siblings.begin(), siblings.end(), node);
            if (it != siblings.end()) {
                siblings.erase(it);
            }
        }
        delete node;
    }

    void SnapshotManager::SaveToFile(const std::string& path) {
        if(!root) return;
        std::ofstream f(path, std::ios::binary);
        if(!f.good()) return;
        // Header
        const char* magic = "CASIOSNAP";
        f.write(magic, 9);
        uint32_t version = 1;
        Binary::Write(f, version);

        root->Write(f);

        // Save current path
        std::vector<uint32_t> path_indices;
        SnapshotNode* p = current;
        while(p && p != root) {
            if(p->parent) {
                auto& siblings = p->parent->children;
                auto it = std::find(siblings.begin(), siblings.end(), p);
                if(it != siblings.end()) {
                    path_indices.push_back((uint32_t)std::distance(siblings.begin(), it));
                }
            }
            p = p->parent;
        }

        uint32_t depth = (uint32_t)path_indices.size();
        Binary::Write(f, depth);
        // Reverse to get path from root
        for(auto it = path_indices.rbegin(); it != path_indices.rend(); ++it) {
            Binary::Write(f, *it);
        }
    }

    void SnapshotManager::LoadFromFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if(!f.good()) return;

        char magic[10] = {0};
        f.read(magic, 9);
        if(strcmp(magic, "CASIOSNAP") != 0) return;

        uint32_t version;
        Binary::Read(f, version);

        Clear(); // Clear existing
        root = new SnapshotNode();
        root->Read(f);

        current = root;
        // Restore current
        uint32_t depth;
        Binary::Read(f, depth);
        for(uint32_t i=0; i<depth; ++i) {
            uint32_t idx;
            Binary::Read(f, idx);
            if(current && idx < current->children.size()) {
                current = current->children[idx];
            }
        }
    }
}

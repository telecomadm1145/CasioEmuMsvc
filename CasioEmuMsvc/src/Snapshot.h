#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <iosfwd>

namespace casioemu {
    class Emulator;
}

// --- SnapshotNode ---
// A single node in the snapshot tree.
struct SnapshotNode {
    uint32_t Id       = 0;
    uint32_t ParentId = 0;     // 0 = root level
    std::string Label;          // user description
    int64_t  Timestamp = 0;    // UTC epoch seconds
    std::vector<uint8_t> CompressedState;   // deflated peripheral state blob
    uint64_t UncompressedStateSize = 0;
    std::vector<uint8_t> PreviewPng;        // PNG screenshot bytes (not compressed again)

    void Write(std::ostream& os) const;
    void Read(std::istream& is);
};

// --- SnapshotManager ---
// Manages the in-memory snapshot tree and file I/O.
class SnapshotManager {
public:
    std::vector<SnapshotNode> Nodes;

    // Save current emulator state as a child of parentId (0 = root).
    // Returns the new node's Id.
    uint32_t SaveSnapshot(casioemu::Emulator& emu, uint32_t parentId, const std::string& label);

    // Restore the emulator to the state stored in node with given Id.
    void LoadSnapshot(casioemu::Emulator& emu, uint32_t id);

    // Export a single node to a .snapshot file.
    void ExportNode(const std::filesystem::path& path, uint32_t id) const;

    // Export this SnapshotManager's entire tree to a .snapshot file.
    void ExportAll(const std::filesystem::path& path) const;

    // Export a subtree rooted at nodeId (inclusive) to a .snapshot file.
    void ExportSubtree(const std::filesystem::path& path, uint32_t rootId) const;

    // Import nodes from a .snapshot file (merges into current tree).
    void ImportFromFile(const std::filesystem::path& path);

    // Delete a node (and all its descendants) by Id.
    void DeleteNode(uint32_t id);

    // Get all child Ids of a given parentId.
    std::vector<uint32_t> GetChildren(uint32_t parentId) const;

private:
    uint32_t m_NextId = 1;
    static constexpr uint32_t k_Magic   = 0x5041534e; // "SNAP"
    static constexpr uint32_t k_Version = 1;

    // Capture the emulator screen as a PNG byte buffer.
    static std::vector<uint8_t> CaptureScreenPng(casioemu::Emulator& emu);

    // Write/read a set of nodes (used for both single and batch export).
    static void WriteNodes(std::ostream& os, const std::vector<SnapshotNode>& nodes);
    static std::vector<SnapshotNode> ReadNodes(std::istream& is);

    // Collect a subtree recursively.
    std::vector<SnapshotNode> CollectSubtree(uint32_t rootId) const;
};

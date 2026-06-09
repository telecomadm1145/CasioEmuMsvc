#include "Snapshot.h"
#include "Compress.h"
#include "Chipset/Chipset.hpp"
#include "Emulator.hpp"

#include <SDL.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

// --- Minimal inline PNG encoder (1-channel grayscale or RGBA) ---
// We use a simple approach: write a BMP-like raw block, but since we need
// something SDL can decode back, we use SDL_SaveBMP_RW to memory.
// For the preview we encode as BMP stored in the vector.

namespace {

// Write a uint32 little-endian to stream
static void WriteU32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}
static void WriteU64(std::ostream& os, uint64_t v) {
    os.write(reinterpret_cast<const char*>(&v), 8);
}
static void WriteStr(std::ostream& os, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    WriteU32(os, len);
    os.write(s.data(), len);
}
static void WriteBuf(std::ostream& os, const std::vector<uint8_t>& buf) {
    uint64_t sz = buf.size();
    WriteU64(os, sz);
    if (sz) os.write(reinterpret_cast<const char*>(buf.data()), sz);
}

static uint32_t ReadU32(std::istream& is) {
    uint32_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 4);
    return v;
}
static uint64_t ReadU64(std::istream& is) {
    uint64_t v = 0;
    is.read(reinterpret_cast<char*>(&v), 8);
    return v;
}
static std::string ReadStr(std::istream& is) {
    uint32_t len = ReadU32(is);
    if (len > 65536) throw std::runtime_error("Snapshot string too long");
    std::string s(len, '\0');
    is.read(s.data(), len);
    return s;
}
static std::vector<uint8_t> ReadBuf(std::istream& is) {
    uint64_t sz = ReadU64(is);
    if (sz > 256ULL * 1024 * 1024) throw std::runtime_error("Snapshot buffer too large");
    std::vector<uint8_t> buf(sz);
    if (sz) is.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

} // anonymous namespace

// ============================================================
// SnapshotNode serialization
// ============================================================

void SnapshotNode::Write(std::ostream& os) const {
    WriteU32(os, Id);
    WriteU32(os, ParentId);
    WriteStr(os, Label);
    os.write(reinterpret_cast<const char*>(&Timestamp), sizeof(Timestamp));
    WriteU64(os, UncompressedStateSize);
    WriteBuf(os, CompressedState);
    WriteBuf(os, PreviewPng);
}

void SnapshotNode::Read(std::istream& is) {
    Id       = ReadU32(is);
    ParentId = ReadU32(is);
    Label    = ReadStr(is);
    is.read(reinterpret_cast<char*>(&Timestamp), sizeof(Timestamp));
    UncompressedStateSize = ReadU64(is);
    CompressedState = ReadBuf(is);
    PreviewPng      = ReadBuf(is);
}

// ============================================================
// SnapshotManager helpers
// ============================================================

void SnapshotManager::WriteNodes(std::ostream& os, const std::vector<SnapshotNode>& nodes) {
    WriteU32(os, k_Magic);
    WriteU32(os, k_Version);
    WriteU64(os, static_cast<uint64_t>(nodes.size()));
    for (const auto& n : nodes)
        n.Write(os);
}

std::vector<SnapshotNode> SnapshotManager::ReadNodes(std::istream& is) {
    uint32_t magic   = ReadU32(is);
    uint32_t version = ReadU32(is);
    if (magic != k_Magic)
        throw std::runtime_error("Invalid snapshot file (bad magic)");
    if (version != k_Version)
        throw std::runtime_error("Unsupported snapshot version: " + std::to_string(version));
    uint64_t count = ReadU64(is);
    if (count > 100000) throw std::runtime_error("Too many snapshot nodes");
    std::vector<SnapshotNode> nodes;
    nodes.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        SnapshotNode n;
        n.Read(is);
        nodes.push_back(std::move(n));
    }
    return nodes;
}

// ============================================================
// Screen capture helper
// ============================================================

std::vector<uint8_t> SnapshotManager::CaptureScreenPng(casioemu::Emulator& emu) {
    SDL_Renderer* renderer = emu.GetRenderer();
    if (!renderer) return {};

    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) return {};

    // Create surface to receive pixel data
    SDL_Surface* surface = SDL_CreateRGBSurface(0, w, h, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surface) return {};

    if (SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
                             surface->pixels, surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        return {};
    }

    // Scale down to ~200x100 for thumbnail (preserve aspect)
    int targetW = 200, targetH = 0;
    if (w > 0 && h > 0) {
        targetH = static_cast<int>(static_cast<float>(h) * targetW / w);
        if (targetH < 1) targetH = 1;
    }
    SDL_Surface* thumb = SDL_CreateRGBSurface(0, targetW, targetH, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (thumb) {
        SDL_Rect dst = {0, 0, targetW, targetH};
        SDL_BlitScaled(surface, nullptr, thumb, &dst);
        SDL_FreeSurface(surface);
        surface = thumb;
    }

    // Save as BMP to memory via SDL_RWops
    SDL_RWops* rw = SDL_AllocRW();
    if (!rw) { SDL_FreeSurface(surface); return {}; }

    // Use a memory buffer via a custom RWops backed by std::vector
    struct MemBuf { std::vector<uint8_t> data; size_t pos = 0; };
    MemBuf buf;

    SDL_RWops* mem = SDL_RWFromMem(nullptr, 0); // placeholder
    SDL_FreeRW(rw);

    // Simpler: just encode BMP to a in-memory SDL_RWops
    // SDL doesn't expose RWFromMem writable easily without a fixed buffer.
    // We'll allocate a large enough buffer.
    size_t bmpBound = static_cast<size_t>(surface->w) * surface->h * 4 + 54 + 1024;
    std::vector<uint8_t> bmpBuf(bmpBound, 0);
    SDL_RWops* rwMem = SDL_RWFromMem(bmpBuf.data(), static_cast<int>(bmpBound));
    if (!rwMem) { SDL_FreeSurface(surface); return {}; }

    int saveResult = SDL_SaveBMP_RW(surface, rwMem, 0);
    Sint64 written = SDL_RWtell(rwMem);
    SDL_RWclose(rwMem);
    SDL_FreeSurface(surface);

    if (saveResult != 0 || written <= 0) return {};
    bmpBuf.resize(static_cast<size_t>(written));
    return bmpBuf;
}

// ============================================================
// Public API
// ============================================================

uint32_t SnapshotManager::SaveSnapshot(casioemu::Emulator& emu, uint32_t parentId, const std::string& label) {
    // Pause emulator during save
    bool wasPaused = emu.GetPaused();
    emu.SetPaused(true);

    // Serialize peripheral + CPU state
    std::ostringstream stateStream(std::ios::binary);
    emu.chipset.SaveStateAll(stateStream);
    std::string rawStr = stateStream.str();

    // Compress
    const uint8_t* rawData = reinterpret_cast<const uint8_t*>(rawStr.data());
    size_t rawSize = rawStr.size();
    auto compressed = Compress::Deflate(rawData, rawSize);

    // Capture screen preview
    auto preview = CaptureScreenPng(emu);

    // Assign Id and timestamp
    SnapshotNode node;
    node.Id       = m_NextId++;
    node.ParentId = parentId;
    node.Label    = label;
    node.Timestamp = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    node.UncompressedStateSize = static_cast<uint64_t>(rawSize);
    node.CompressedState = std::move(compressed);
    node.PreviewPng      = std::move(preview);

    uint32_t newId = node.Id;
    Nodes.push_back(std::move(node));

    emu.SetPaused(wasPaused);
    return newId;
}

void SnapshotManager::LoadSnapshot(casioemu::Emulator& emu, uint32_t id) {
    auto it = std::find_if(Nodes.begin(), Nodes.end(),
        [id](const SnapshotNode& n) { return n.Id == id; });
    if (it == Nodes.end())
        throw std::runtime_error("Snapshot not found: " + std::to_string(id));

    bool wasPaused = emu.GetPaused();
    emu.SetPaused(true);

    // Decompress
    auto raw = Compress::Inflate(
        it->CompressedState.data(),
        it->CompressedState.size(),
        static_cast<size_t>(it->UncompressedStateSize));

    // Deserialize
    std::istringstream is(std::string(reinterpret_cast<const char*>(raw.data()), raw.size()),
                          std::ios::binary);
    emu.chipset.LoadStateAll(is);

    emu.SetPaused(wasPaused);
}

void SnapshotManager::ExportNode(const std::filesystem::path& path, uint32_t id) const {
    auto it = std::find_if(Nodes.begin(), Nodes.end(),
        [id](const SnapshotNode& n) { return n.Id == id; });
    if (it == Nodes.end())
        throw std::runtime_error("Snapshot not found: " + std::to_string(id));

    std::ofstream fs(path, std::ios::binary);
    if (!fs) throw std::runtime_error("Cannot open file for writing: " + path.string());
    WriteNodes(fs, { *it });
}

void SnapshotManager::ExportAll(const std::filesystem::path& path) const {
    std::ofstream fs(path, std::ios::binary);
    if (!fs) throw std::runtime_error("Cannot open file for writing: " + path.string());
    WriteNodes(fs, Nodes);
}

std::vector<SnapshotNode> SnapshotManager::CollectSubtree(uint32_t rootId) const {
    std::vector<SnapshotNode> result;
    std::vector<uint32_t> toVisit = { rootId };
    while (!toVisit.empty()) {
        uint32_t cur = toVisit.back(); toVisit.pop_back();
        for (const auto& n : Nodes) {
            if (n.Id == cur) result.push_back(n);
            if (n.ParentId == cur && n.Id != cur) toVisit.push_back(n.Id);
        }
    }
    return result;
}

void SnapshotManager::ExportSubtree(const std::filesystem::path& path, uint32_t rootId) const {
    auto subtree = CollectSubtree(rootId);
    std::ofstream fs(path, std::ios::binary);
    if (!fs) throw std::runtime_error("Cannot open file for writing: " + path.string());
    WriteNodes(fs, subtree);
}

void SnapshotManager::ImportFromFile(const std::filesystem::path& path) {
    std::ifstream fs(path, std::ios::binary);
    if (!fs) throw std::runtime_error("Cannot open snapshot file: " + path.string());
    auto nodes = ReadNodes(fs);
    // Re-map Ids to avoid collisions
    for (auto& n : nodes) {
        uint32_t newId = m_NextId++;
        // Update children's parentId
        for (auto& m : nodes) {
            if (m.ParentId == n.Id) m.ParentId = newId;
        }
        n.Id = newId;
        Nodes.push_back(std::move(n));
    }
}

void SnapshotManager::DeleteNode(uint32_t id) {
    // Collect entire subtree to delete
    auto subtree = CollectSubtree(id);
    std::vector<uint32_t> toDelete;
    for (const auto& n : subtree) toDelete.push_back(n.Id);
    Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(),
        [&](const SnapshotNode& n) {
            return std::find(toDelete.begin(), toDelete.end(), n.Id) != toDelete.end();
        }), Nodes.end());
}

std::vector<uint32_t> SnapshotManager::GetChildren(uint32_t parentId) const {
    std::vector<uint32_t> result;
    for (const auto& n : Nodes)
        if (n.ParentId == parentId && n.Id != parentId)
            result.push_back(n.Id);
    return result;
}

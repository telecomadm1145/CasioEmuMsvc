#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
#include "miniz.h"

namespace Compress {

// Compress raw bytes using miniz deflate. Returns compressed bytes.
inline std::vector<uint8_t> Deflate(const uint8_t* data, size_t size) {
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(size));
    std::vector<uint8_t> out(bound);
    mz_ulong outSize = bound;
    int ret = mz_compress2(out.data(), &outSize, data, static_cast<mz_ulong>(size), MZ_BEST_COMPRESSION);
    if (ret != MZ_OK)
        throw std::runtime_error("Compress::Deflate failed: " + std::to_string(ret));
    out.resize(outSize);
    return out;
}

// Decompress bytes previously compressed with Deflate.
// originalSize must be the exact uncompressed size.
inline std::vector<uint8_t> Inflate(const uint8_t* data, size_t compressedSize, size_t originalSize) {
    std::vector<uint8_t> out(originalSize);
    mz_ulong outSize = static_cast<mz_ulong>(originalSize);
    int ret = mz_uncompress(out.data(), &outSize, data, static_cast<mz_ulong>(compressedSize));
    if (ret != MZ_OK)
        throw std::runtime_error("Compress::Inflate failed: " + std::to_string(ret));
    return out;
}

} // namespace Compress

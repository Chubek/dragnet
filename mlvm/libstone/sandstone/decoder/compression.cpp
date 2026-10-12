// Sandstone payload compression (SymAS payload layer).
//
// Layering (see SYMAS.md "Archiving and Compression"):
//   archive/container layer -> handled by the caller (zstd framing etc.)
//   SymAS payload layer     -> format-defined id in the file header:
//                              0 = none, 1 = zlib, 2 = zstd.
//
// This translation unit drives zlib and libzstd directly. When the build
// disables zstd (SANDSTONE_HAS_ZSTD unset), requesting zstd compression or
// opening a zstd bank fails with DOMSYMAS_ERR_BAD_COMPRESSION instead of
// silently producing an unreadable file.
#include "sandstone.hpp"

#include <cstring>

#include <zlib.h>

#if defined(SANDSTONE_HAS_ZSTD)
#include <zstd.h>
#endif

namespace domsymas {

namespace {

// 64 MiB cap, matching the historical domarchive options used here.
constexpr size_t kMaxOutput = 64u << 20;

} // namespace

Error compress_payload(const std::vector<uint8_t> &in, Compression c,
                       std::vector<uint8_t> &out) {
    if (c == Compression::None) {
        out = in;
        return Error{};
    }
    if (in.size() > kMaxOutput)
        return Error{DOMSYMAS_ERR_OVERFLOW, "payload exceeds 64 MiB", 0};
    if (c == Compression::Zlib) {
        uLong bound = compressBound((uLong)in.size());
        if (bound > kMaxOutput)
            return Error{DOMSYMAS_ERR_OVERFLOW, "compressed bound exceeds 64 MiB", 0};
        out.resize(bound ? bound : 1);
        uLong dest = bound;
        int rc = compress2(out.data(), &dest, in.data(), (uLong)in.size(),
                           Z_BEST_COMPRESSION);
        if (rc != Z_OK)
            return Error{DOMSYMAS_ERR_BAD_COMPRESSION, "zlib compression failed", 0};
        out.resize((size_t)dest);
        return Error{};
    }
    if (c == Compression::Zstd) {
#if defined(SANDSTONE_HAS_ZSTD)
        size_t bound = ZSTD_compressBound(in.size());
        if (bound > kMaxOutput)
            return Error{DOMSYMAS_ERR_OVERFLOW, "compressed bound exceeds 64 MiB", 0};
        out.resize(bound ? bound : 1);
        size_t rc = ZSTD_compress(out.data(), out.size(), in.data(), in.size(),
                                 ZSTD_CLEVEL_DEFAULT);
        if (ZSTD_isError(rc))
            return Error{DOMSYMAS_ERR_BAD_COMPRESSION,
                         std::string("zstd compression failed: ") +
                             ZSTD_getErrorName(rc),
                         0};
        out.resize(rc);
        return Error{};
#else
        (void)in;
        out.clear();
        return Error{DOMSYMAS_ERR_BAD_COMPRESSION,
                     "zstd requested but this build has no zstd support", 0};
#endif
    }
    return Error{DOMSYMAS_ERR_BAD_COMPRESSION, "unknown compression method", 0};
}

Error decompress_payload(const uint8_t *data, size_t len, Compression c,
                         size_t expected, std::vector<uint8_t> &out) {
    if (c == Compression::None) {
        if (len != expected)
            return Error{DOMSYMAS_ERR_TRUNCATED, "uncompressed size mismatch", 0};
        out.assign(data, data + len);
        return Error{};
    }
    if (expected > kMaxOutput)
        return Error{DOMSYMAS_ERR_OVERFLOW, "payload exceeds 64 MiB", 0};
    if (c == Compression::Zlib) {
        out.resize(expected ? expected : 1);
        uLong dest = (uLong)expected;
        int rc = uncompress(out.data(), &dest, data, (uLong)len);
        if (rc != Z_OK)
            return Error{DOMSYMAS_ERR_BAD_COMPRESSION, "zlib decompression failed",
                         0};
        if ((size_t)dest != expected) {
            // uncompress() succeeds only when the buffer is exactly filled,
            // but keep the explicit check so truncated or padded streams are
            // diagnosed even if zlib semantics change.
            return Error{DOMSYMAS_ERR_TRUNCATED, "decompressed size mismatch", 0};
        }
        out.resize(expected);
        return Error{};
    }
    if (c == Compression::Zstd) {
#if defined(SANDSTONE_HAS_ZSTD)
        out.resize(expected ? expected : 1);
        size_t rc = ZSTD_decompress(out.data(), expected, data, len);
        if (ZSTD_isError(rc))
            return Error{DOMSYMAS_ERR_BAD_COMPRESSION,
                         std::string("zstd decompression failed: ") +
                             ZSTD_getErrorName(rc),
                         0};
        if (rc != expected)
            return Error{DOMSYMAS_ERR_TRUNCATED, "decompressed size mismatch", 0};
        out.resize(expected);
        return Error{};
#else
        (void)data;
        (void)len;
        out.clear();
        return Error{DOMSYMAS_ERR_BAD_COMPRESSION,
                     "zstd bank but this build has no zstd support", 0};
#endif
    }
    return Error{DOMSYMAS_ERR_BAD_COMPRESSION, "unknown compression method", 0};
}

} // namespace domsymas

#include "ra2assets/mappack.hpp"
#include "ra2assets/lcw.hpp"
#include "ra2assets/lzo1x.hpp"
#include <cctype>
#include <stdexcept>

// Map-pack wrapper: base64 decode + "Format5" chunk decompression, sitting on
// top of the library's two block codecs. RA2/YR store several packed sections
// of a map INI as base64 text; decoded, each yields a "Format5" stream of
// independently compressed chunks ([u16 compressedSize][u16 uncompressedSize]
// [body]*). The chunk WRAPPER is shared across sections; the per-chunk body
// codec differs by section (confirmed against XCC map_ts_encoder.cpp ->
// decode5(...,format)):
//   - [IsoMapPack5]  (terrain, an array of 11-byte cell records): bodies are
//                    LZO1X  (ra2assets::lzo1x_decompress; XCC decode5(...,5)).
//   - [OverlayPack]/[OverlayDataPack]: bodies are LCW / "Format80"
//                    (ra2assets::lcw_decompress; XCC decode5(...,80)).
namespace ra2assets {

std::vector<uint8_t> base64_decode(const std::string& s) {
    // Lookup table mapping each ASCII byte to its 6-bit value in the
    // standard base64 alphabet, or -1 if it isn't part of it.
    static int8_t table[256];
    static bool table_ready = false;
    if (!table_ready) {
        for (auto& v : table) v = -1;
        const char* alpha =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";
        for (int i = 0; i < 64; i++) table[(unsigned char)alpha[i]] = (int8_t)i;
        table_ready = true;
    }

    std::vector<uint8_t> out;
    out.reserve(s.size() * 3 / 4 + 4);
    uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : s) {
        // '=' padding only ever appears as a suffix of the final 4-char
        // group; once seen, any bits already accumulated in `buf` beyond a
        // full byte are simply padding and are correctly discarded below by
        // never being emitted.
        if (c == '=') break;
        if (std::isspace(c)) continue;
        int8_t v = table[c];
        if (v < 0) throw std::runtime_error("base64_decode: invalid character in input");
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

std::vector<uint8_t> decompress_mappack(const uint8_t* data, size_t len, MapPackCodec codec) {
    std::vector<uint8_t> out;
    size_t pos = 0;
    while (pos < len) {
        if (pos + 4 > len)
            throw std::runtime_error(
                "format5: truncated chunk header at offset " + std::to_string(pos));
        uint16_t cmp   = (uint16_t)(data[pos] | (uint16_t)(data[pos + 1] << 8));
        uint16_t uncmp = (uint16_t)(data[pos + 2] | (uint16_t)(data[pos + 3] << 8));
        pos += 4;
        // A map pack is bounded by the 512x512 cell format (terrain <3 MiB,
        // overlays 256 KiB). Leave ample room for previews while rejecting
        // cumulative expansion before allocating/decompressing another chunk.
        constexpr size_t max_pack_bytes=16u*1024u*1024u;
        if(uncmp>max_pack_bytes-out.size())
            throw std::runtime_error("format5: decoded pack exceeds 16 MiB limit");
        if (pos + cmp > len)
            throw std::runtime_error(
                "format5: chunk body (size=" + std::to_string(cmp) +
                ") overruns input at offset " + std::to_string(pos));

        std::vector<uint8_t> tmp(uncmp);
        // Dispatch the chunk body to the codec selected for this section. Both
        // decoders share the same (src, src_len, dst, dst_len)->bytes-written
        // contract and each bounds-checks itself internally.
        size_t produced = (codec == MapPackCodec::LZO)
            ? lzo1x_decompress(data + pos, cmp, tmp.data(), uncmp)
            : lcw_decompress(data + pos, cmp, tmp.data(), uncmp);
        if (produced != (size_t)uncmp)
            throw std::runtime_error(
                "format5: chunk at offset " + std::to_string(pos) + " decompressed to " +
                std::to_string(produced) + " bytes, header declared " + std::to_string(uncmp));
        out.insert(out.end(), tmp.begin(), tmp.end());
        pos += cmp;
    }
    return out;
}

std::vector<uint8_t> decompress_format5(const uint8_t* data, size_t len) {
    return decompress_mappack(data, len, MapPackCodec::LCW);
}

} // namespace ra2assets

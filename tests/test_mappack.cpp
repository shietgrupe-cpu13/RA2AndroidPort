#include "doctest.h"
#include "ra2assets/mappack.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace ra2assets;

TEST_CASE("base64 known vectors"){
    auto a=base64_decode("TWFu"); CHECK(std::string(a.begin(),a.end())=="Man");
    auto b=base64_decode("cGxlYXN1cmUu"); CHECK(std::string(b.begin(),b.end())=="pleasure.");
}
TEST_CASE("format5 single + double chunk"){
    // lcw("HELLO") = 0x85 'H' 'E' 'L' 'L' 'O' 0x80  (literal-5 then end) => 7 bytes, uncmp=5
    std::vector<uint8_t> lcw={0x85,'H','E','L','L','O',0x80};
    auto chunk=[&](std::vector<uint8_t> body,uint16_t u){ std::vector<uint8_t> c;
        c.push_back(body.size()&0xff);c.push_back(body.size()>>8);c.push_back(u&0xff);c.push_back(u>>8);
        for(auto x:body)c.push_back(x); return c; };
    auto one=chunk(lcw,5); auto out=decompress_format5(one.data(),one.size());
    CHECK(std::string(out.begin(),out.end())=="HELLO");
    auto two=one; for(auto x:chunk(lcw,5)) two.push_back(x);
    auto out2=decompress_format5(two.data(),two.size()); CHECK(std::string(out2.begin(),out2.end())=="HELLOHELLO");
}

// --- CARRY-FORWARD from Task 1 (P2): real-data LCW/Format80 validation ---
// Task 1's lcw_decompress was only exercised on hand-built synthetic
// streams. Real Format80-compressed data first becomes available here, as
// the "Format5"-wrapped chunks inside a real map's INI.
//
// IMPORTANT CORRECTION vs. the task brief's premise: the brief assumed
// [IsoMapPack5] itself is LCW/Format80 data. Investigating this (see
// task-2-report.md) against XCC's OWN reference source
// (D:\ra2-re\reference\xcc\misc\shp_decode.cpp: decode5(...,format), used
// by D:\ra2-re\reference\xcc\misc\map_ts_encoder.cpp) shows the "5" in
// "IsoMapPack5" names an XCC *pack format id*: format 5 dispatches to
// decode5s() -> LZO1X (lzo1x_decompress), NOT decode80()/LCW. Format 80
// (real LCW, what this file implements) is what [OverlayPack] and
// [OverlayDataPack] use -- same [u16 size_in][u16 size_out] chunk header
// (cc_structures.h: t_pack_section_header), same decode5() chunk loop,
// just a different per-chunk body codec selected by that format id.
// [OverlayPack]/[OverlayDataPack] are therefore the correct real-world
// source of genuine Format80/LCW-compressed Format5 chunks, and are what
// this test validates against. (IsoMapPack5's LZO1X body is out of scope
// for this task -- flagged in task-2-report.md as a finding for whichever
// later task parses real IsoMapPack5 terrain.)
namespace {
// Minimal inline extractor: pulls the numbered-key values out of a map
// INI's [<section>] and concatenates them in file order. Every stock map
// inspected writes these keys in ascending sequential order (1,2,3,...),
// so this is NOT a general INI parser (Task 5 owns that) -- just enough to
// hand base64_decode a single concatenated blob.
std::string extract_section_base64(const std::string& text, const std::string& section) {
    const std::string marker = "[" + section + "]";
    size_t start = text.find(marker);
    if (start == std::string::npos) return "";
    start += marker.size();
    std::string out;
    std::istringstream lines(text.substr(start));
    std::string line;
    while (std::getline(lines, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '[') break; // reached the next INI section
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue; // be lenient, skip stray lines
        out += line.substr(eq + 1);
    }
    return out;
}
} // namespace

TEST_CASE("format5: real map OverlayPack decompresses (closes Task 1 real-data LCW gap)") {
    // Guard: point RA2_TEST_MAP at a real extracted .map text file (see
    // task-2-report.md for how one was pulled out of mapsmd03.mix via
    // ra2extract --raw). Skips gracefully -- does not fail -- if absent,
    // so the suite stays green on machines without the RA2 install/asset.
    const char* path = std::getenv("RA2_TEST_MAP");
    if (!path) { MESSAGE("skipped: RA2_TEST_MAP env var not set"); return; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { MESSAGE("skipped: cannot open RA2_TEST_MAP=" << path); return; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.empty()) { MESSAGE("skipped: RA2_TEST_MAP file is empty"); return; }

    std::string b64 = extract_section_base64(text, "OverlayPack");
    if (b64.empty()) { MESSAGE("skipped: no [OverlayPack] section found in RA2_TEST_MAP"); return; }

    auto packed = base64_decode(b64);
    REQUIRE(packed.size() > 0);

    // (a) Must decompress without throwing -- this IS the real-data
    // Format80/LCW exercise deferred from Task 1.
    auto cells = decompress_format5(packed.data(), packed.size());
    CHECK(cells.size() > 0);

    // (b) decompress_format5 internally requires each chunk's produced byte
    // count to match its header's declared uncompressed size exactly
    // (throws std::runtime_error otherwise), so simply reaching here proves
    // every chunk matched -- across every ~8192-byte chunk in a real,
    // multi-chunk stream, not just one hand-built vector. The remaining
    // structural sanity check: TS-engine OverlayPack is always a flat
    // one-byte-per-cell array over the engine's fixed 512x512 map grid, so
    // total decompressed bytes must be a positive multiple of 512 -- a
    // wrong LCW decoder would very likely desync this alignment on real,
    // larger data even if it happened to pass the synthetic tests above.
    REQUIRE(cells.size() % 512 == 0);
    CHECK(cells.size() / 512 > 0);
    MESSAGE("real map OverlayPack: " << packed.size() << " packed bytes -> " << cells.size()
            << " decompressed bytes");
}

TEST_CASE("map pack: cumulative chunk expansion is bounded before allocation") {
 std::vector<uint8_t> packed;
 for(int n=0;n<257;++n) {
  // One valid LCW fill of 65535 zero bytes per Format5 chunk.
  for(uint8_t b:{5,0,255,255,254,255,255,0,128})packed.push_back(b);
 }
 CHECK_THROWS_WITH(ra2assets::decompress_format5(packed.data(),packed.size()),
                   "format5: decoded pack exceeds 16 MiB limit");
}

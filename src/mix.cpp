#include "ra2assets/mix.hpp"
#include "ra2assets/asset_trace.hpp"
#include "ra2assets/blowfish.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>

namespace ra2assets {

static uint32_t rd32(const uint8_t* p){ return p[0]|(p[1]<<8)|(p[2]<<16)|(uint32_t(p[3])<<24); }
static uint16_t rd16(const uint8_t* p){ return p[0]|(p[1]<<8); }

namespace detail {
// W2-GA-P5: where a MixFile's bytes live -- an owned buffer (open(bytes)) or
// a file on disk read with offset reads (open_path). Shared by every nested
// view, so a child archive never copies its parent's bytes.
struct MixSource {
    std::string path="memory";
    std::vector<uint8_t> mem;      // memory-backed
    std::FILE* f = nullptr;        // file-backed
    uint64_t file_size = 0;
    std::mutex mu;                 // file reads may come from the music thread
    ~MixSource() { if (f) std::fclose(f); }
    uint64_t size() const { return f ? file_size : mem.size(); }
    void read(uint64_t pos, void* dst, size_t n) {
        if (pos + n > size()) throw std::runtime_error("mix: entry out of range");
        if (!f) { std::memcpy(dst, mem.data() + pos, n); return; }
        std::lock_guard<std::mutex> lk(mu);
#if defined(_WIN32)
        if (_fseeki64(f, (long long)pos, SEEK_SET) != 0) throw std::runtime_error("mix: seek failed");
#else
        if (fseeko(f, (off_t)pos, SEEK_SET) != 0) throw std::runtime_error("mix: seek failed");
#endif
        if (n && std::fread(dst, 1, n, f) != n) throw std::runtime_error("mix: short read");
    }
};
}  // namespace detail

void MixFile::read_raw(uint64_t pos, void* dst, size_t n) const {
    if (pos + n > size_) throw std::runtime_error("mix: entry out of range");
    src_->read(base_ + pos, dst, n);
}

MixFile MixFile::open(std::vector<uint8_t> bytes) {
    auto src = std::make_shared<detail::MixSource>();
    src->mem = std::move(bytes);
    const uint64_t n = src->mem.size();
    return open_view(std::move(src), 0, n);
}

MixFile MixFile::open_path(const std::string& path) {
    trace_asset("open-archive",path,"disk",0);
    // One shared handle per path (the loaders open ra2.mix / ra2md.mix many
    // times); weak so a closed game releases the file.
    static std::mutex reg_mu;
    static std::map<std::string, std::weak_ptr<detail::MixSource>> reg;
    std::shared_ptr<detail::MixSource> src;
    {
        std::lock_guard<std::mutex> lk(reg_mu);
        auto it = reg.find(path);
        if (it != reg.end()) src = it->second.lock();
        if (!src) {
            std::FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) throw std::runtime_error("cannot open file: " + path);
            src = std::make_shared<detail::MixSource>();
            src->f = f;
            src->path = path;
#if defined(_WIN32)
            _fseeki64(f, 0, SEEK_END);
            const long long sz = _ftelli64(f);
#else
            fseeko(f, 0, SEEK_END);
            const long long sz = (long long)ftello(f);
#endif
            if (sz < 0) throw std::runtime_error("cannot stat file: " + path);
            src->file_size = (uint64_t)sz;
            reg[path] = src;
        }
    }
    const uint64_t n = src->size();
    return open_view(std::move(src), 0, n);
}

MixFile MixFile::open_loose_path(const std::string& name, const std::string& path) {
    auto source=std::make_shared<detail::MixSource>();
    source->f=std::fopen(path.c_str(),"rb");
    if(!source->f)throw std::runtime_error("cannot open loose resource: "+path);
    source->path=path;
#if defined(_WIN32)
    _fseeki64(source->f,0,SEEK_END);const auto size=_ftelli64(source->f);
#else
    fseeko(source->f,0,SEEK_END);const auto size=ftello(source->f);
#endif
    if(size<0 || static_cast<uint64_t>(size)>64*1024*1024)throw std::runtime_error("invalid loose resource size: "+path);
    source->file_size=static_cast<uint64_t>(size);
    MixFile m;m.src_=std::move(source);m.size_=static_cast<uint64_t>(size);
    m.entries_.push_back({mix_id(name),0,static_cast<uint32_t>(size)});return m;
}

MixFile MixFile::open_view(std::shared_ptr<detail::MixSource> src, uint64_t base, uint64_t size) {
    MixFile m;
    m.src_ = std::move(src);
    m.base_ = base;
    m.size_ = size;
    const uint64_t n = size;
    if (n < 4) throw std::runtime_error("mix: too small");
    uint8_t head[4];
    m.read_raw(0, head, 4);
    uint32_t first = rd32(head);
    size_t pos;
    if ((first & 0x0000FFFF) == 0) {          // new format: flags dword
        uint32_t flags = first;
        m.encrypted_   = (flags & 0x00020000) != 0;
        m.checksummed_ = (flags & 0x00010000) != 0;
        pos = 4;
        if (m.encrypted_) {
            // Layout after flags: 80-byte key source, then Blowfish-encrypted
            // header+index in 8-byte blocks.
            if (n < 4 + 80 + 8) throw std::runtime_error("mix: truncated encrypted header");
            uint8_t ks[80];
            m.read_raw(pos, ks, 80);                     // pos == 4
            uint8_t key[56];
            detail::westwood_blowfish_key(ks, key);
            Blowfish bf; bf.set_key(key, 56);
            pos += 80;

            // Decrypt first block (8 bytes) to read count+bodysize (6 bytes).
            uint8_t blk0[8], hdr[8];
            m.read_raw(pos, blk0, 8);
            bf.decrypt_block(blk0, hdr);
            uint16_t count = rd16(hdr);
            // header is 6 bytes; index is count*12 bytes; total rounded up to /8.
            size_t index_bytes = size_t(count) * 12;
            size_t total = 6 + index_bytes;
            size_t blocks = (total + 7) / 8;
            // A wrong Blowfish key (a botched Task 2 port) yields a garbage
            // count. Fail loudly and diagnosably instead of reading out of
            // bounds — this is the primary tripwire for the key-derivation port.
            if (pos + blocks * 8 > n)
                throw std::runtime_error("mix: encrypted index exceeds file (wrong key derivation?)");
            std::vector<uint8_t> enc(blocks * 8), dec(blocks * 8);
            m.read_raw(pos, enc.data(), enc.size());
            for (size_t b = 0; b < blocks; b++)
                bf.decrypt_block(enc.data() + b*8, dec.data() + b*8);
            // body starts after the encrypted region (block-aligned)
            m.body_start_ = pos + blocks * 8;
            const uint8_t* d = dec.data();
            /* count */ (void)rd16(d);
            /* bodysize */ (void)rd32(d + 2);
            size_t ip = 6;
            m.entries_.reserve(count);
            for (uint16_t i = 0; i < count; i++) {
                MixEntry e{ rd32(d+ip), rd32(d+ip+4), rd32(d+ip+8) };
                ip += 12;
                m.entries_.push_back(e);
            }
            return m;
        }
    } else {                                   // old TD/RA1 format: starts with count
        m.encrypted_ = false; m.checksummed_ = false;
        pos = 0;
    }
    if (n < pos + 6) throw std::runtime_error("mix: truncated header");
    uint8_t h6[6];
    m.read_raw(pos, h6, 6);
    uint16_t count = rd16(h6); pos += 2;
    /* body size */ pos += 4;
    if ((uint64_t)pos + (uint64_t)count * 12 > n) throw std::runtime_error("mix: truncated index");
    std::vector<uint8_t> idx(size_t(count) * 12);
    if (!idx.empty()) m.read_raw(pos, idx.data(), idx.size());
    m.entries_.reserve(count);
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t* q = idx.data() + size_t(i) * 12;
        MixEntry e{ rd32(q), rd32(q+4), rd32(q+8) };
        m.entries_.push_back(e);
    }
    m.body_start_ = pos + size_t(count) * 12;
    return m;
}

const MixEntry* MixFile::find(uint32_t id) const {
    for (auto& e : entries_) if (e.id == id) return &e;
    return nullptr;
}

std::vector<uint8_t> MixFile::read(const MixEntry& e) const {
    uint64_t start = (uint64_t)body_start_ + e.offset;
    if (start + e.size > size_) throw std::runtime_error("mix: entry out of range");
    std::vector<uint8_t> out(e.size);
    if (e.size) read_raw(start, out.data(), e.size);
    return out;
}

uint32_t mix_id(const std::string& name_in) {
    // TS/RA2/YR scheme — NOT the TD/RA1 rotate-add scheme. Uppercase the name;
    // if the length is not a multiple of 4, append ONE byte whose value is
    // (len & 3), then repeat the FIRST character of the final 4-byte block
    // until the length is a multiple of 4; then take a standard CRC32
    // (reflected poly 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF).
    // Reference: XCC `get_id` (Cc_structures); OpenRA PackageHashType.CRC32.
    std::string s;
    s.reserve(name_in.size() + 4);
    for (char c : name_in) s.push_back((char)std::toupper((unsigned char)c));
    size_t l = s.size();
    if (l & 3u) {
        s.push_back((char)(l & 3u));
        size_t block_start = l & ~size_t(3);   // first char of the final block
        while (s.size() & 3u) s.push_back(s[block_start]);
    }
    uint32_t crc = 0xFFFFFFFFu;                // standard CRC32, bitwise form
    for (unsigned char ch : s) {
        crc ^= ch;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

const MixEntry* MixFile::find(const std::string& name) const {
    const auto* entry=find(mix_id(name));
    if(entry && asset_trace_callback().load(std::memory_order_relaxed))trace_asset("file-hit",name,src_->path+"@"+std::to_string(base_),entry->size);
    return entry;
}

std::vector<std::string> MixFile::local_names() const {
    // "local mix database.dat" id, then parse its XCC-format string table.
    const MixEntry* e = find(mix_id("local mix database.dat"));
    std::vector<std::string> out;
    if (!e) return out;
    auto data = read(*e);
    // XCC local db format: 32-byte header ("XCC by Olaf van der Spek\0" + type),
    // uint32 size, uint32 count, then `count` NUL-terminated names.
    if (data.size() < 0x34) return out;
    const uint8_t* p = data.data();
    uint32_t count = rd32(p + 0x30);
    size_t pos = 0x34;
    for (uint32_t i = 0; i < count && pos < data.size(); i++) {
        std::string nm;
        while (pos < data.size() && p[pos]) nm.push_back((char)p[pos++]);
        pos++; // skip NUL
        out.push_back(nm);
    }
    return out;
}

MixFile MixFile::open_nested(const std::string& name) const {
    trace_asset("open-nested",name,"archive-view@"+std::to_string(base_),size_);
    const MixEntry* e = find(name);
    if (!e) throw std::runtime_error("mix: nested entry not found: " + name);
    // W2-GA-P5: a view into the same source -- no copy of the child's bytes.
    const uint64_t start = (uint64_t)body_start_ + e->offset;
    if (start + e->size > size_) throw std::runtime_error("mix: entry out of range");
    return MixFile::open_view(src_, base_ + start, e->size);
}

} // namespace ra2assets

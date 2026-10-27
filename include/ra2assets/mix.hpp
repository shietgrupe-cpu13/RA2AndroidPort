#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
#include <string>

namespace ra2assets::detail {
// Derives up to 56 key bytes from the 80-byte MIX key-source block via
// Westwood's fixed public-key decryption (a.k.a. get_blowfish_key).
// Returns the number of key bytes produced (56 for standard WW MIX).
size_t westwood_blowfish_key(const uint8_t src[80], uint8_t out_key[56]);
} // namespace ra2assets::detail

namespace ra2assets {
struct MixEntry { uint32_t id; uint32_t offset; uint32_t size; };
namespace detail { struct MixSource; }
class MixFile {
public:
    static MixFile open(std::vector<uint8_t> bytes);
    // W2-GA-P5: open a MIX on disk WITHOUT reading it whole. Only the header
    // and index are read; entries are read on demand (file offset reads), and
    // open_nested() returns a VIEW into the same file instead of copying the
    // child archive. Opening the same path twice shares one handle. Throws
    // like read_file ("cannot open file: <path>") when absent.
    static MixFile open_path(const std::string& path);
    // One loose resource exposed through the existing offset reader (no packing/copy).
    static MixFile open_loose_path(const std::string& name, const std::string& path);
    bool encrypted() const { return encrypted_; }
    bool checksummed() const { return checksummed_; }
    size_t count() const { return entries_.size(); }
    const std::vector<MixEntry>& entries() const { return entries_; }
    const MixEntry* find(uint32_t id) const;
    const MixEntry* find(const std::string& name) const;
    std::vector<uint8_t> read(const MixEntry& e) const;
    // Names listed inside "local mix database.dat", if this MIX embeds one.
    std::vector<std::string> local_names() const;
    // Extract a child MIX by name and open it.
    MixFile open_nested(const std::string& name) const;
private:
    static MixFile open_view(std::shared_ptr<detail::MixSource> src, uint64_t base, uint64_t size);
    void read_raw(uint64_t pos, void* dst, size_t n) const;   // pos relative to base_
    std::shared_ptr<detail::MixSource> src_;
    uint64_t base_ = 0;     // archive start within the source
    uint64_t size_ = 0;     // archive length
    std::vector<MixEntry> entries_;
    size_t body_start_ = 0;
    bool encrypted_ = false, checksummed_ = false;
};
uint32_t mix_id(const std::string& name);
} // namespace ra2assets

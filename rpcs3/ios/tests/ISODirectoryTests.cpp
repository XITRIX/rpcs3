#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using u8 = uint8_t; using s8 = int8_t; using u16 = uint16_t;
using u32 = uint32_t; using u64 = uint64_t; using s64 = int64_t; using usz = size_t;
using namespace std::string_view_literals;

template<class T> struct be_t
{
    u8 bytes[sizeof(T)];
    operator T() const { T v = 0; for (u8 b : bytes) v = T((v << 8) | b); return v; }
};
namespace utils { template<class T> const T* bless(const void* p) { return static_cast<const T*>(p); } }
std::string utf16_to_utf8(const std::u16string& text)
{
    std::string out;
    for (auto c : text) { assert(c < 128); out.push_back(static_cast<char>(c)); }
    return out;
}
struct { template<class... T> void error(T...) {} } iso_log;
namespace fs
{
struct file
{
    std::vector<u8> bytes; u64 cursor = 0;
    u64 pos() const { return cursor; }
    void seek(u64 p) { cursor = p; }
    template<class T> bool read(T& value)
    {
        if (cursor + sizeof(T) > bytes.size()) return false;
        std::memcpy(&value, bytes.data() + cursor, sizeof(T)); cursor += sizeof(T); return true;
    }
    bool read(std::string& value, u64 count)
    {
        if (cursor + count > bytes.size()) return false;
        value.assign(reinterpret_cast<const char*>(bytes.data() + cursor), count); cursor += count; return true;
    }
};
struct dir_entry
{
    std::string name; s64 atime = 0, mtime = 0, ctime = 0;
    bool is_directory = false, is_symlink = false, is_writable = false; u64 size = 0;
};
}
struct iso_fs_node;
struct iso_dir
{
    const iso_fs_node& m_node; usz m_pos = 0;
    bool read(fs::dir_entry&); void rewind();
};
// Match the production predicate shorthand for these directory-entry types.
#define FN(expr) [&](const fs::dir_entry& x, [[maybe_unused]] const fs::dir_entry& y = fs::dir_entry{}) { return expr; }
#include "production.inc"
u64 iso_fs_metadata::size() const { u64 total = 0; for (auto e : extents) total += e.size; return total; }

iso_fs_metadata parse(std::string_view raw, bool joliet = false, bool directory = false)
{
    fs::file file;
    file.bytes.resize(33 + raw.size() + (raw.size() % 2 == 0));
    file.bytes[0] = static_cast<u8>(file.bytes.size());
    file.bytes[2] = 18; file.bytes[9] = 18; // both-endian extent
    file.bytes[18] = 126; file.bytes[19] = 10; file.bytes[20] = 13;
    file.bytes[25] = directory ? 2 : 0;
    file.bytes[32] = static_cast<u8>(raw.size());
    std::copy(raw.begin(), raw.end(), file.bytes.begin() + 33);
    bool error = true;
    auto result = iso_read_directory_entry(file, error, joliet);
    assert(result && !error && file.pos() == file.bytes.size());
    assert(result->is_directory == directory && result->extents[0].start == 18);
    return std::move(*result);
}

int main()
{
    unsigned cases = 0;
    for (bool joliet : {false, true})
    {
        iso_fs_node root;
        for (auto raw : {std::string_view("\0", 1), std::string_view("\1", 1)})
            root.children.push_back(std::make_unique<iso_fs_node>(iso_fs_node{parse(raw, joliet, true), {}}));
        root.children.push_back(std::make_unique<iso_fs_node>(iso_fs_node{parse("english.lang;1"), {}}));
        iso_dir dir{root};
        std::vector<fs::dir_entry> entries;
        fs::dir_entry entry;
        while (dir.read(entry)) entries.push_back(entry);
        assert(entries.size() == 3);
        // Execute actual sys_fs_opendir ordering. A duplicate dot after the
        // first two entries is precisely what sent Doom 3 back into itself.
        order_entries(entries);
        if (entries.size() != 3)
        {
            for (const auto& item : entries) fprintf(stderr, "[%s] ", item.name.c_str());
            fprintf(stderr, "\n");
        }
        assert(entries.size() == 3);
        assert(entries[0].name == "." && entries[1].name == "..");
        assert(entries[2].name == "english.lang" && !entries[2].is_directory);
        dir.rewind(); assert(dir.read(entry) && entry.name == ".");
        ++cases;
    }
    for (const auto& [raw, expected] : std::vector<std::pair<std::string, std::string>>{
        {"TEST.BIN;1", "TEST.BIN"}, {"README.;1", "README"}, {"README.", "README"},
        {"A;1", "A"}, {".hidden;1", ".hidden"}, {"...;1", ".."}})
    {
        assert(parse(raw).name == expected); ++cases;
        std::string encoded;
        for (char c : raw) { encoded.push_back('\0'); encoded.push_back(c); }
        assert(parse(encoded, true).name == expected); ++cases;
    }
    printf("ISO directory: %u parser/enumeration/ordering cases passed.\n", cases);
}

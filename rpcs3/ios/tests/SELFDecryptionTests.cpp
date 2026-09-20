// The runner injects the unchanged production DecryptData and WriteElf bodies.
#include "aes.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usz = size_t;
struct max_value { template <typename T> constexpr operator T() const { return std::numeric_limits<T>::max(); } };
constexpr max_value umax{};
void ensure(bool value) { if (!value) throw std::runtime_error("production ensure failed"); }
template <typename T, typename V> T narrow(V value) { ensure(value <= std::numeric_limits<T>::max()); return static_cast<T>(value); }
template <typename T> T read_from_ptr(const std::vector<u8>& bytes, usz offset)
{
    ensure(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
    T value; std::memcpy(&value, bytes.data() + offset, sizeof(T)); return value;
}
template <typename T> const T& at32(const std::vector<T>& values, u32 index) { return values.at(index); }
template <typename T> u32 size32(const T& values) { return narrow<u32>(values.size()); }
struct logger
{
    template <typename... Args> void error(const char*, Args...) const {}
    template <typename... Args> void warning(const char*, Args...) const {}
} self_log;
namespace fs
{
struct file
{
    std::vector<u8> bytes;
    mutable usz position = 0;
    bool short_read = false;
    u64 size() const { return bytes.size(); }
    void seek(u64 offset) const { position = narrow<usz>(offset); }
    usz read(void* destination, usz count) const
    {
        const usz available = position <= bytes.size() ? bytes.size() - position : 0;
        usz amount = std::min(count, available);
        if (short_read && amount) --amount;
        if (amount) std::memcpy(destination, bytes.data() + position, amount);
        position += amount;
        return amount;
    }
    void write(const void* source, usz count)
    {
        bytes.resize(std::max(bytes.size(), position + count));
        if (count) std::memcpy(bytes.data() + position, source, count);
        position += count;
    }
};
}
struct MetadataSectionHeader
{
    u64 data_offset{}, data_size{};
    u32 type{2}, program_idx{}, hashed{}, sha1_idx{}, encrypted{1}, key_idx{}, iv_idx{}, compressed{1};
};
struct EHdr { u64 e_shoff = 0; };
struct PHdr { u64 p_offset{}, p_filesz{}; };
struct SHdr {};
void WriteEhdr(fs::file&, EHdr) {}
void WritePhdr(fs::file&, PHdr) {}
void WriteShdr(fs::file&, SHdr) {}
struct SELFDecrypter
{
    fs::file self_f;
    std::vector<MetadataSectionHeader> meta_shdr;
    struct { u32 key_count = 2; } meta_hdr;
    struct { u64 shdr_offset = 0; } m_ext_hdr;
    std::vector<u8> data_keys, data_buf;
    bool DecryptData();
#include "SELFWriterUnderTest.inc"
};
#include "SELFDataUnderTest.inc"

std::vector<u8> hex(const std::string& text)
{
    std::vector<u8> result;
    for (usz i = 0; i < text.size(); i += 2) result.push_back(std::stoul(text.substr(i, 2), nullptr, 16));
    return result;
}
const auto clear = hex("6bc1bee22e409f96e93d7e117393172a");
const auto cipher = hex("874d6191b620e3261bef6864990db6ce");
SELFDecrypter fixture()
{
    SELFDecrypter result;
    // NIST SP 800-38A AES-128 CTR known-answer block.
    result.data_keys = hex("2b7e151628aed2a6abf7158809cf4f3cf0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    return result;
}
void add(SELFDecrypter& reader, const std::vector<u8>& bytes, u32 encrypted, u32 compressed = 1, u32 type = 2)
{
    const auto offset = reader.self_f.bytes.size();
    reader.self_f.bytes.insert(reader.self_f.bytes.end(), bytes.begin(), bytes.end());
    MetadataSectionHeader section;
    section.data_offset = offset;
    section.data_size = bytes.size();
    section.encrypted = encrypted;
    section.compressed = compressed;
    section.type = type;
    section.program_idx = std::count_if(reader.meta_shdr.begin(), reader.meta_shdr.end(), [](const auto& h) { return h.type == 2; });
    section.key_idx = encrypted == 3 ? 0 : UINT32_MAX;
    section.iv_idx = encrypted == 3 ? 1 : UINT32_MAX;
    reader.meta_shdr.push_back(section);
}
void mixed_segments()
{
    const std::vector<u8> note(52, 0x5a);
    for (bool interleave_metadata : {false, true})
    for (u32 plain_position : {2u, 0u, 1u})
    {
        auto reader = fixture();
        std::vector<u8> expected;
        std::vector<PHdr> headers;
        for (u32 index = 0; index < 3; ++index)
        {
            const bool plain = index == plain_position;
            add(reader, plain ? note : cipher, plain ? 1 : 3);
            headers.push_back({0x100 + expected.size(), plain ? note.size() : clear.size()});
            const auto& bytes = plain ? note : clear;
            expected.insert(expected.end(), bytes.begin(), bytes.end());
            // Non-program metadata must not enter the packed program buffer.
            if (interleave_metadata) add(reader, cipher, 3, 1, 1);
        }
        assert(reader.DecryptData());
        fs::file elf;
        reader.WriteElf(elf, EHdr{}, std::vector<SHdr>{}, headers);
        assert(reader.data_buf == expected);
        assert(elf.bytes.size() == 0x100 + expected.size());
        assert(std::equal(expected.begin(), expected.end(), elf.bytes.begin() + 0x100));
    }
}
void plain_and_compressed()
{
    auto reader = fixture();
    std::vector<u8> payload(97);
    for (usz i = 0; i < payload.size(); ++i) payload[i] = static_cast<u8>(i);
    std::vector<u8> compressed(compressBound(payload.size()));
    uLongf size = compressed.size();
    assert(compress2(compressed.data(), &size, payload.data(), payload.size(), Z_BEST_SPEED) == Z_OK);
    compressed.resize(size);
    add(reader, compressed, 1, 2);
    add(reader, {}, 1);
    add(reader, payload, 1);
    assert(reader.DecryptData());
    fs::file elf;
    reader.WriteElf(elf, EHdr{}, std::vector<SHdr>{}, std::vector<PHdr>{{0x100, payload.size()}, {0x200, 0}, {0x300, payload.size()}});
    assert(std::equal(payload.begin(), payload.end(), elf.bytes.begin() + 0x100));
    assert(std::equal(payload.begin(), payload.end(), elf.bytes.begin() + 0x300));
    auto encrypted = fixture();
    add(encrypted, cipher, 3);
    assert(encrypted.DecryptData() && encrypted.data_buf == clear);
}
void invalid_input()
{
    auto base = fixture(); add(base, cipher, 3);
    auto bad = base; bad.meta_shdr[0].iv_idx = bad.meta_hdr.key_count;
    assert(!bad.DecryptData());
    bad = base; bad.meta_shdr[0].key_idx = bad.meta_hdr.key_count;
    assert(!bad.DecryptData());
    bad = base; bad.meta_shdr[0].encrypted = 2;
    assert(!bad.DecryptData());
    bad = base; bad.meta_shdr[0].data_size++;
    assert(!bad.DecryptData());
    bad = base; bad.meta_shdr[0].data_offset = UINT64_MAX;
    assert(!bad.DecryptData());
    bad = base; bad.meta_shdr[0].data_size = UINT64_MAX;
    assert(!bad.DecryptData());
    bad = base; bad.self_f.short_read = true;
    assert(!bad.DecryptData());
}
std::vector<u8> read_file(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    ensure(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void captured_fixture(const std::string& directory)
{
    auto reader = fixture();
    reader.self_f.bytes = read_file(directory + "/source.bin");
    reader.data_keys = read_file(directory + "/keys.bin");
    reader.meta_hdr.key_count = size32(reader.data_keys) / 16;
    std::ifstream sections(directory + "/sections.txt");
    MetadataSectionHeader h;
    while (sections >> h.data_offset >> h.data_size >> h.type >> h.program_idx >> h.hashed >> h.sha1_idx >> h.encrypted >> h.key_idx >> h.iv_idx >> h.compressed)
        reader.meta_shdr.push_back(h);
    assert(!reader.meta_shdr.empty());
    assert(reader.DecryptData());
    assert(reader.data_buf == read_file(directory + "/expected-programs.bin"));
    std::cout << "Captured SELF program bytes match independent reconstruction\n";
}
int main(int argc, char** argv)
{
    mixed_segments();
    plain_and_compressed();
    invalid_input();
    if (argc == 2) captured_fixture(argv[1]);
    std::cout << "SELF mixed plaintext/encrypted, metadata ordering, AES, zlib and invalid-input checks passed\n";
}

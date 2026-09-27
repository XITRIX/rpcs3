#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>
using u8=uint8_t; using u64=uint64_t;
constexpr u64 ISO_SECTOR_SIZE=2048;
struct logger { template<class... A> void error(A...) {} template<class... A> void warning(A...) {} } iso_log;
struct file
{
    std::vector<u8> data;
    unsigned reads=0,fail_on=0; u64 partial=0; bool raw=false,last_complete=true;
    u64 failed_offset=0;
    explicit file(u64 size=64*2048):data(size) { for(u64 i=0;i<data.size();++i) data[i]=u8((i*19+i/997)%251); }
    u64 read_at(u64 offset,void* dest,u64 count)
    {
        if(raw) assert(offset%2048==0 && count%2048==0 && reinterpret_cast<uintptr_t>(dest)%2048==0);
        ++reads;auto n=std::min<u64>(count,offset<data.size()?data.size()-offset:0);
        if(reads==fail_on) { n=std::min(n,std::min(partial,count-1));failed_offset=offset; }
        last_complete=n==count;
        if(n) std::memcpy(dest,data.data()+offset,n);
        return n;
    }
};
struct cipher
{
    file* input; unsigned calls=0;
    void decrypt(u64 offset,std::span<u8> bytes,const char*)
    {
        assert(input->last_complete); // Failed reads must never reach decryption.
        assert(offset%16==0 && bytes.size()%16==0);
        ++calls;for(auto& v:bytes) v^=0xa5;
    }
};
struct extent { u64 archive,length; };
struct iso_file
{
    file m_file; bool m_raw_device=false;
    struct { const char* name="fixture"; } m_meta;
    std::vector<extent> extents;
    u64 size() const { u64 n=0;for(auto x:extents)n+=x.length;return n; }
    u64 local_extent_remaining(u64 offset) const { for(auto x:extents) { if(offset<x.length)return x.length-offset;offset-=x.length; }return 0; }
    u64 file_offset(u64 offset) const { for(auto x:extents) { if(offset<x.length)return x.archive+offset;offset-=x.length; }assert(false);return 0; }
    virtual u64 read_at(u64,void*,u64);
    virtual ~iso_file()=default;
};
struct iso_file_encrypted:iso_file
{
    cipher dec{&m_file};cipher* m_dec=&dec;
    u64 read_at(u64,void*,u64) override;
};
void* get_aligned_buf();
#include "production.inc"
void* get_aligned_buf() { alignas(ISO_SECTOR_SIZE) static u8 bytes[ISO_RAW_READ_SIZE];return bytes; }
int main()
{
    unsigned cases=0;
    for(bool encrypted:{false,true}) for(bool raw:{false,true})
    for(u64 start:{0,1,15,16,17,2047,2048,2059})
    for(u64 size:{0,1,15,16,17,2047,2048,2049,8193})
    {
        iso_file plain;iso_file_encrypted enc;iso_file& f=encrypted?static_cast<iso_file&>(enc):plain;
        f.m_raw_device=f.m_file.raw=raw;
        f.extents={{2048+start,6001},{32768+13,6000}};
        std::vector<u8> buffer(size+2,0xcc);
        auto result=f.read_at(0,buffer.data()+1,size);
        assert(result==size && buffer.front()==0xcc && buffer.back()==0xcc);
        for(u64 i=0;i<size;++i) assert(buffer[i+1]==(f.m_file.data[f.file_offset(i)]^(encrypted?0xa5:0)));
        const unsigned read_count=f.m_file.reads;
        for(unsigned fail=1;fail<=read_count;++fail) for(u64 partial:{0,1,15})
        {
            f.m_file.reads=0;f.m_file.fail_on=fail;f.m_file.partial=partial;
            std::fill(buffer.begin(),buffer.end(),0xcc);enc.dec.calls=0;
            result=f.read_at(0,buffer.data()+1,size);
            // An earlier extent may already have succeeded when the next one fails.
            assert(result<size);assert(f.m_file.reads==fail);
            assert(buffer.front()==0xcc && buffer.back()==0xcc);
            if(fail==1 && (encrypted || raw)) assert(std::all_of(buffer.begin(),buffer.end(),[](u8 b){return b==0xcc;}));
            ++cases;
        }
        f.m_file.fail_on=0;f.m_file.reads=0;
        assert(f.read_at(f.size(),buffer.data()+1,size)==0 && f.m_file.reads==0);
        ++cases;
    }
    // Batched raw reads, including multiple full chunks and the remainder, must
    // reject short reads before copying/decrypting stale scratch-buffer bytes.
    for(bool encrypted:{false,true}) for(u64 size:{u64{8192},2*ISO_RAW_READ_SIZE+3*ISO_SECTOR_SIZE})
    {
        iso_file plain;iso_file_encrypted enc;iso_file& f=encrypted?static_cast<iso_file&>(enc):plain;
        f.m_file=file(size);f.extents={{0,size}};f.m_raw_device=f.m_file.raw=true;
        std::vector<u8> data(size+2,0xcc);
        assert(f.read_at(0,data.data()+1,size)==size);
        assert(data.front()==0xcc && data.back()==0xcc);
        for(u64 i=0;i<size;++i)assert(data[i+1]==(f.m_file.data[i]^(encrypted?0xa5:0)));
        const unsigned read_count=f.m_file.reads;
        assert(read_count==2+(size-2*ISO_SECTOR_SIZE+ISO_RAW_READ_SIZE-1)/ISO_RAW_READ_SIZE);
        for(unsigned fail=1;fail<=read_count;++fail) for(u64 partial:{u64{0},u64{7},ISO_SECTOR_SIZE-1,ISO_RAW_READ_SIZE-1})
        {
            f.m_file.reads=0;f.m_file.fail_on=fail;f.m_file.partial=partial;enc.dec.calls=0;
            std::fill(data.begin(),data.end(),0xcc);
            assert(f.read_at(0,data.data()+1,size)==0 && f.m_file.reads==fail);
            assert(data.front()==0xcc && data.back()==0xcc);
            assert(std::all_of(data.begin()+1+f.m_file.failed_offset,data.end(),[](u8 b){return b==0xcc;}));
            if(encrypted)assert(enc.dec.calls==fail-1);
            ++cases;
        }
        ++cases;
    }
    // Regular encrypted bulk-inner failure must not decrypt its partial direct read.
    iso_file_encrypted enc;enc.extents={{0,8192}};enc.m_file.fail_on=2;enc.m_file.partial=7;
    std::vector<u8> buffer(8192,0xcc);assert(enc.read_at(0,buffer.data(),8192)==0);
    assert(enc.dec.calls==1);
    for(unsigned i=0;i<7;++i)assert(buffer[2048+i]==enc.m_file.data[2048+i]);
    for(unsigned i=2055;i<8192;++i)assert(buffer[i]==0xcc);
    printf("ISO read: %u extent/alignment/short-read cases passed.\n",cases+1);
}

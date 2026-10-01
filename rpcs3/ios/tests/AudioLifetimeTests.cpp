#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>
#include <mutex>
using u32=uint32_t;using u64=uint64_t;using f32=float;using error_code=u32;
using std::shared_ptr;using std::make_shared;
constexpr u32 CELL_OK=0,CELL_EEXIST=1,CELL_ESRCH=2,CELL_EINVAL=3;
constexpr u32 AUDIO_PORT_COUNT=8,AUDIO_BUFFER_SAMPLES=256,SYS_SYNC_PROCESS_SHARED=0,SYS_SYNC_NEWLY_CREATED=0;
constexpr u64 SYS_MMAPPER_MIO_SHM_KEY=99;
#include "constants.inc"
struct checked_mutex{bool held=false;void lock(){assert(!held);held=true;}void unlock(){assert(held);held=false;}};
struct hle_locks_t{bool try_lock(){return true;}void unlock(){}};
namespace cpu_flag{constexpr u32 again=1,wait=2;}
void unlocked();
struct cpu_state
{
 u32 bits=0;
 void operator+=(u32 flags){bits|=flags;}
 void operator=(u32 flags){bits=flags;}
 u32 operator&(u32 flags)const{return bits&flags;}
};
struct ppu_thread{cpu_state state;void check_state(){unlocked();state.bits&=~cpu_flag::wait;}};
ppu_thread* current_ppu=nullptr;
unsigned vm_wait_checks=0;
void require_vm_wait()
{
 unlocked();
 // vm::get, block_t::peek and dealloc all acquire vm::writer_lock. A
 // registered, running PPU must set wait before entering that lock.
 assert(current_ppu && (current_ppu->state&cpu_flag::wait));
 ++vm_wait_checks;
}
template<auto Function,class... Args>auto call(ppu_thread& p,Args... args)
{
 auto* previous=std::exchange(current_ppu,&p);
 assert(!(p.state&cpu_flag::wait));
 const auto result=Function(p,args...);
 // HLE return processes state before guest code makes its next audio call.
 p.check_state();
 current_ppu=previous;
 return result;
}
struct backing{std::vector<unsigned char>data;explicit backing(u32 size):data(size,0x99){}void*get(){return data.data();}};
namespace utils{using shm=backing;}
struct backing_slot{std::shared_ptr<backing>value;auto load()const{return std::make_shared<std::shared_ptr<backing>>(value);}auto observe()const{return &value;}};
struct lv2_memory_container{};
struct lv2_memory{u32 size,id,system_handle=0,refs=0,counter=0;bool handle=true;backing_slot shm;lv2_memory(u32 n,u32,u32,u64,bool,lv2_memory_container*):size(n),id(0),shm{std::make_shared<backing>(n)}{}};
std::map<u32,shared_ptr<lv2_memory>>memories;std::map<u32,shared_ptr<lv2_memory>>mappings;u32 next_id=1,next_addr=0x10000,last_id=0;
int fail_at=0,operation=0;unsigned allocations=0,unmaps=0;std::function<void()>mapping_hook,dealloc_hook;
void unlocked();bool fail(){unlocked();return fail_at&&++operation==fail_at;}
namespace vm{
 template<class T>struct var{mutable T value{};T&operator*()const{return value;}T*operator+()const{return &value;}operator T*()const{return &value;}};
 template<class T>struct ptr{T*value;ptr(T*p):value(p){}ptr(const var<T>&v):value(+v){}operator bool()const{return value;}T*operator->()const{return value;}T&operator*()const{return *value;}};
 constexpr int any=0;constexpr u32 mapping_comp=1;struct area{u32 flags=mapping_comp;bool dealloc(u32 addr,const std::shared_ptr<backing>*expected){require_vm_wait();if(dealloc_hook)dealloc_hook();auto it=mappings.find(addr);if(it==mappings.end()||it->second->shm.value!=*expected)return false;mappings.erase(it);++unmaps;return true;}auto peek(u32 addr){require_vm_wait();auto it=mappings.find(addr);return std::pair{addr,it==mappings.end()?std::shared_ptr<backing>{}:it->second->shm.value};}};
 auto get(int,u32){require_vm_wait();return std::make_shared<area>();}u32 cast(u32 x){return x;}
 void*base(u32 addr){return mappings.at(addr)->shm.value->get();}
}
struct guest_address{u32 value=0;u32 addr()const{return value;}void operator=(u32 x){value=x;}};
enum class audio_port_state{closed,opened,started};
struct level_slot{void store(std::pair<float,float>){}};
struct audio_port{audio_port_state state=audio_port_state::closed;bool mapped=false;u32 number=0,server_index=0,num_channels=0,num_blocks=0,size=0,cur_pos=0,index=0;u64 attr=0,global_counter=0,active_counter=0,timestamp=0;float level=0;level_slot level_set;guest_address addr;};
struct audio_port_mapping{shared_ptr<lv2_memory>memory;u32 area=0,addr=0,number=0,server_index=0;u64 generation=0;bool mapped=false;};
struct cell_audio_thread{
 checked_mutex mutex;bool memory_transition=false;u32 init=0,free_port_count=0,shared_refs=0,shared_address=0,shared_area=0;u64 m_generation=0,m_counter=0,m_last_period_end=0;
 std::array<audio_port,8>ports;std::array<shared_ptr<lv2_memory>,8>port_memories;shared_ptr<lv2_memory>shared_memory;
 std::array<u64,8>guest_timestamps{};
 std::array<u32,8>free_ports{},free_indices{},m_periods_without_tag{};std::array<bool,8>m_front_only_reported{},m_front_only_port{};
 audio_port*open_port();static u32 port_alloc_size(u32);static error_code map_port(ppu_thread&,u32,audio_port_mapping&);
 audio_port_mapping detach_port(audio_port&);static bool reuse_port_memory(ppu_thread&,audio_port_mapping&,u32);audio_port_mapping take_kept_memory(audio_port&);void close_port(audio_port&);
 static void unmap_port(ppu_thread&,audio_port_mapping&);void return_port(const audio_port_mapping&);u32 drop_shared_ref();static void unmap_shared(ppu_thread&,u32,const shared_ptr<lv2_memory>& = {});
}audio;
using cell_audio=cell_audio_thread;
void unlocked(){assert(!audio.mutex.held);}
struct{u32 sdk_ver=0x400000;}g_ps3_process_info;
template<class T,class K>struct ipc_manager{bool get(K){return false;}};
struct fxo{template<class T>T&get(){if constexpr(std::is_same_v<T,cell_audio>)return audio;else{static T x;return x;}}}storage;auto*g_fxo=&storage;
namespace idm{template<class T>u32 last_id(){return ::last_id;}
struct selected{shared_ptr<lv2_memory>memory;u32 ret=0;operator bool()const{return bool(memory);}lv2_memory*operator->()const{return memory.get();}};
template<class Base,class T,class F>selected select(F f){for(auto&[id,m]:memories)if(auto result=f(id,*m))return {m,result};return {};}}

struct lv2_obj{template<class T,class F>static error_code create(u32,u64,u32,F f){if(fail())return CELL_ESRCH;auto m=f();m->id=last_id=next_id++;memories[m->id]=m;++allocations;return 0;}};
namespace utils{u32 align(u32 x,u32 a){return (x+a-1)&~(a-1);}}
template<class T,class U>T narrow(U x){return static_cast<T>(x);}
struct logger{template<class...T>void warning(T...){}template<class...T>void todo(T...){}template<class...T>void trace(T...){}}cellAudio,sys_mmapper;
void lv2_sleep(u32)
{
 assert(current_ppu);
 const bool had_wait=current_ppu->state&cpu_flag::wait;
 current_ppu->state+=cpu_flag::wait;
 current_ppu->check_state();
 if(had_wait)current_ppu->state+=cpu_flag::wait;
}
u64 get_guest_system_time(u64 x){return x;}
struct CellAudioPortParam{u64 nChannel=2,nBlock=8,attr=0;float level=1;};
shared_ptr<lv2_memory>acquire_audio_memory(u32 id){unlocked();auto m=memories.at(id);++m->refs;return m;}
void collect(u32 id){auto m=memories.at(id);if(!m->handle&&!m->counter&&!m->refs)memories.erase(id);}
void release_audio_memory(shared_ptr<lv2_memory>&m){unlocked();if(m){auto id=m->id;assert(m->refs);--m->refs;m.reset();collect(id);}}
error_code sys_mmapper_free_shared_memory(ppu_thread&,u32 id){unlocked();auto m=memories.at(id);m->handle=false;collect(id);return 0;}
error_code sys_mmapper_search_and_map(ppu_thread& p,u32,u32 id,u32,u32*out){p.state+=cpu_flag::wait;require_vm_wait();if(mapping_hook)mapping_hook();if(fail())return CELL_ESRCH;auto m=memories.at(id);*out=next_addr;next_addr+=m->size;++m->counter;mappings[*out]=m;return 0;}
error_code sys_mmapper_get_shared_memory_area(ppu_thread&,u64,u32*out){if(fail())return CELL_ESRCH;*out=1;return 0;}
error_code sys_mmapper_allocate_shared_memory(ppu_thread&,u64,u32 size,u32,u32*out){if(fail())return CELL_EEXIST;auto m=make_shared<lv2_memory>(size,0,0,0,false,nullptr);*out=m->id=next_id++;memories[*out]=m;++allocations;return 0;}
// Upstream's suspend-aware lock only differs from lock() under contention; the fixture is single-threaded.
std::unique_lock<checked_mutex> lock_audio(checked_mutex& mutex){return std::unique_lock(mutex);}
#define ensure(x) assert(x)
#include "production.inc"
void empty(){assert(memories.empty()&&mappings.empty()&&!audio.memory_transition&&!audio.shared_refs);}
void failed_init(ppu_thread&p,int stage){fail_at=stage;operation=0;assert(call<cellAudioInit>(p));assert(!audio.init);empty();fail_at=0;}
int main(){
 ppu_thread p;for(int stage=1;stage<=3;stage++)failed_init(p,stage);
 assert(!call<cellAudioInit>(p));assert(call<cellAudioInit>(p)==CELL_AUDIO_ERROR_ALREADY_INIT);CellAudioPortParam param;u32 number=99;
 auto open=[&]{return call<cellAudioPortOpen>(p,&param,&number);};
 // Mapping can yield to other HLE calls: none may publish or recycle this reserved port.
 unsigned overlap=0;mapping_hook=[&]{++overlap;ppu_thread other;u32 other_number=99;
  assert(call<cellAudioPortClose>(other,0)==CELL_AUDIO_ERROR_PORT_NOT_OPEN);
  assert(!call<cellAudioQuit>(other)&&(other.state&cpu_flag::again));other.state=0;
  assert(!call<cellAudioInit>(other)&&(other.state&cpu_flag::again));other.state=0;
  assert(!call<cellAudioPortOpen>(other,&param,&other_number)&&(other.state&cpu_flag::again)&&other_number==99);
 };
 assert(!open()&&number==0&&overlap==1);mapping_hook={};auto first=audio.port_memories[0];auto addr=audio.ports[0].addr.addr();auto count=allocations;
 assert(audio.ports[0].state==audio_port_state::opened&&audio.ports[0].mapped);assert(!call<cellAudioPortClose>(p,number));
 std::memset(first->shm.value->get(),0x55,first->size);assert(!open()&&allocations==count&&audio.port_memories[0]==first);
 assert(std::all_of(first->shm.value->data.begin(),first->shm.value->data.end(),[](auto v){return !v;}));
 assert(!call<cellAudioPortClose>(p,number));param.nChannel=8;param.nBlock=32;assert(!open()&&audio.port_memories[0]->size==0x40000&&allocations==count+1);
 assert(!call<cellAudioPortClose>(p,number));first.reset();assert(!call<cellAudioQuit>(p));empty();
 // Terminator's intro opens port 0, then opens/closes/reopens Bink port 1.
 // Each call starts as a running PPU; earlier mmapper calls cannot supply wait.
 assert(!call<cellAudioInit>(p));param={};assert(!open()&&number==0);
 audio.ports[0].state=audio_port_state::started;
 assert(!open()&&number==1);auto bink=audio.port_memories[1];
 assert(!call<cellAudioPortClose>(p,1));
 std::memset(bink->shm.value->get(),0x55,bink->size);
 const auto reuse_checks=vm_wait_checks;count=allocations;const auto prior_unmaps=unmaps;
 assert(!open()&&number==1&&allocations==count&&unmaps==prior_unmaps);
 assert(vm_wait_checks==reuse_checks+2&&audio.port_memories[1]==bink);
 assert(audio.ports[0].state==audio_port_state::started);
 assert(std::all_of(bink->shm.value->data.begin(),bink->shm.value->data.end(),[](auto v){return !v;}));
 assert(!call<cellAudioQuit>(p));empty();
 // A guest replacement at the retained address must survive reuse and Quit unchanged.
 assert(!call<cellAudioInit>(p));param={};assert(!open());addr=audio.ports[0].addr.addr();assert(!call<cellAudioPortClose>(p,0));auto original=audio.port_memories[0];
 mappings.erase(addr);--original->counter;original->handle=false;
 auto replacement=make_shared<lv2_memory>(0x10000,0,0,0,false,nullptr);replacement->id=next_id++;replacement->counter=1;memories[replacement->id]=replacement;mappings[addr]=replacement;
 assert(!open());assert(mappings.at(addr)==replacement&&replacement->shm.value->data.front()==0x99);assert(!call<cellAudioQuit>(p));assert(mappings.at(addr)==replacement);
 mappings.erase(addr);memories.erase(replacement->id);empty();
 assert(!call<cellAudioInit>(p));assert(!open());addr=audio.ports[0].addr.addr();original=audio.port_memories[0];
 replacement=make_shared<lv2_memory>(0x10000,0,0,0,false,nullptr);replacement->id=next_id++;replacement->counter=1;memories[replacement->id]=replacement;
 dealloc_hook=[&]{if(mappings.contains(addr)&&mappings.at(addr)==original){--original->counter;original->handle=false;mappings[addr]=replacement;}};
 assert(!call<cellAudioQuit>(p));dealloc_hook={};assert(mappings.at(addr)==replacement&&replacement->shm.value->data.front()==0x99);
 mappings.erase(addr);memories.erase(replacement->id);empty();
 // Allocation and mapping failures return the slot once, then permit a clean retry.
 for(int stage=1;stage<=2;stage++){assert(!call<cellAudioInit>(p));fail_at=stage;operation=0;assert(open()==CELL_AUDIO_ERROR_SHAREDMEMORY);assert(audio.free_port_count==8);fail_at=0;assert(!open());assert(!call<cellAudioQuit>(p));empty();}
 // Retained closed ports are included in Quit; repeated close cannot duplicate indices.
 for(int session=0;session<50;session++){assert(!call<cellAudioInit>(p));for(unsigned i=0;i<8;i++){assert(!open());assert(number==i);}assert(open()==CELL_AUDIO_ERROR_PORT_FULL);
  for(unsigned i=0;i<8;i++){assert(!call<cellAudioPortClose>(p,i));assert(call<cellAudioPortClose>(p,i)==CELL_AUDIO_ERROR_PORT_NOT_OPEN);}assert(audio.free_port_count==8);assert(!call<cellAudioQuit>(p));empty();}
 assert(call<cellAudioQuit>(p)==CELL_AUDIO_ERROR_NOT_INIT);assert(!audio.drop_shared_ref());
 puts("Audio lifetime: production Init/Open/Close/Quit, PPU wait contract, Terminator port-1 reopen, overlapping calls, reuse/growth, guest replacement, failure rollback and 50 reboot cycles passed.");
}

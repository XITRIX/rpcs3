#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>
using namespace std::literals;
using u64=uint64_t;using u32=uint32_t;struct umax_t{template<class T>constexpr operator T()const{return ~T{};}};constexpr umax_t umax{};
using shared_mutex=std::mutex;
template<class T>using named_thread=T;
template<class T>struct atomic_t{T value{};T load()const{return value;}void operator=(T v){value=v;}T fetch_add(T v){return std::exchange(value,value+v);}bool compare_and_swap_test(T before,T after){if(value!=before)return false;value=after;return true;}};
enum class cpu_flag:u32{pending=1,pending_recheck=2,dbg_pause=4};
template<class T>struct bs_t{u32 bits=0;bs_t()=default;bs_t(T x):bits(u32(x)){}explicit bs_t(u32 x):bits(x){}bool operator&(T x)const{return bits&u32(x);}void operator-=(T x){bits&=~u32(x);}void operator+=(T x){bits|=u32(x);}void operator+=(bs_t x){bits|=x.bits;}};
bs_t<cpu_flag>operator+(cpu_flag a,cpu_flag b){return bs_t<cpu_flag>(u32(a)|u32(b));}
struct flags:bs_t<cpu_flag>{unsigned notifications=0;void notify_one(){++notifications;}template<class F>void atomic_op(F f){f(*this);}};
constexpr u32 SPU_EVENT_TM=1,SPU_EVENT_LR=2,SPU_EVENT_INTR_BUSY_CHECK=7;
u64 clock_ticks=0;u64 get_timebased_time(){return clock_ticks;}u64 get_system_time(){return clock_ticks;}
struct{struct{u32 clocks_scale=100,mfc_transfers_timeout=0,mfc_transfers_shuffling=0;}core;}g_cfg;
struct events{u32 mask=0;bool count=false;events load()const{return *this;}};
struct spu_thread{
 u32 id=1,pc=0x100,srr0=0,branch=0,deliver=0,polled=0,lr_polls=0,cpu_work_iteration_count=0,current_bp_pc=0;
 bool interrupt_requires_escape=true,in_cpu_work=false,allow_interrupts_in_cpu_work=false,interrupts_enabled=false,is_dec_frozen=false,unsavable=false,has_active_local_bps=false;
 u32 ch_dec_value=0,mfc_size=0;u64 ch_dec_start_timestamp=0,mfc_last_timestamp=0,dec_intr_deadline=umax;
 atomic_t<u64>dec_intr_armed{};flags state;events ch_events;unsigned char local_breakpoints[32768]{};
 std::function<void()> before_pending_clear;
 void arm_dec_interrupt();void cancel_dec_interrupt();void set_interrupt_status(bool);bool check_state_with_interrupts(bool may_escape=true);
 std::pair<u32,u32>read_dec()const;bool check_mfc_interrupts(u32);void cpu_work();
 bool check_state(){cpu_work();return false;}bool do_mfc(bool,bool){if(before_pending_clear)before_pending_clear();return false;}
 void get_events(u32 mask){++polled;if(mask&SPU_EVENT_LR)++lr_polls;ch_events.count|=bool(mask&deliver);}
 template<class T>T _ref(u32)const{return branch;}
};
struct spu_dec_intr_timer;
struct fxo{template<class T>T&get();} storage;auto*g_fxo=&storage;
std::map<u32,std::shared_ptr<spu_thread>> threads;
namespace idm{template<class T,class F>auto get(u32 id,F){auto it=threads.find(id);return it==threads.end()?std::shared_ptr<T>{}:it->second;}}
enum class thread_state{running,aborting};
namespace thread_ctrl{
 bool aborted=false;unsigned waits=0;std::vector<u64>sleep_times;std::function<void(unsigned)> step;
 auto state(){return aborted?thread_state::aborting:thread_state::running;}
 void wait_for(u64 duration){sleep_times.push_back(duration);step(++waits);}
 template<class T>void notify(T&){}
}
struct escaped{};namespace spu_runtime{void g_escape(spu_thread*){throw escaped{};}}
#include "production.inc"
spu_dec_intr_timer timer;
template<class T>T&fxo::get(){static_assert(std::is_same_v<T,spu_dec_intr_timer>);return timer;}
void run_timer(std::function<void(unsigned)> step){thread_ctrl::aborted=false;thread_ctrl::waits=0;thread_ctrl::sleep_times.clear();thread_ctrl::step=std::move(step);timer();}
int main(){
 auto s=std::make_shared<spu_thread>();threads[1]=s;s->ch_events.mask=SPU_EVENT_TM;s->ch_dec_value=80;
 s->set_interrupt_status(true);auto token=s->dec_intr_armed.load();assert(token&&timer.armed.size()==1);s->arm_dec_interrupt();assert(s->dec_intr_armed.load()==token);
 run_timer([&](unsigned n){if(n==1)clock_ticks=80;else if(n==2){assert(s->state.bits==0);clock_ticks=81;}else thread_ctrl::aborted=true;});
 assert(!s->dec_intr_armed.load());assert(s->state.bits==3);assert(s->state.notifications==1);assert(thread_ctrl::sleep_times[1]==1);
 // A canceled/replaced timer and a reused ID must never wake another countdown.
 clock_ticks=100;s->ch_dec_start_timestamp=100;s->ch_dec_value=100;s->state.bits=0;s->arm_dec_interrupt();token=s->dec_intr_armed.load();
 s->set_interrupt_status(false);run_timer([&](unsigned n){clock_ticks=201;if(n>1)thread_ctrl::aborted=true;});assert(s->state.bits==0);
 clock_ticks=300;s->ch_dec_start_timestamp=300;s->set_interrupt_status(true);assert(s->dec_intr_armed.load()!=token);
 auto replacement=std::make_shared<spu_thread>();threads[1]=replacement;
 run_timer([&](unsigned n){clock_ticks=401;if(n>1)thread_ctrl::aborted=true;});assert(replacement->state.bits==0);
 threads[1]=s;clock_ticks=500;s->ch_dec_start_timestamp=500;s->ch_dec_value=0;s->arm_dec_interrupt();assert(timer.armed.front().due==501);
 s->is_dec_frozen=true;s->arm_dec_interrupt();assert(!s->dec_intr_armed.load());s->is_dec_frozen=false;s->ch_events.mask=0;s->arm_dec_interrupt();assert(!s->dec_intr_armed.load());
 // Restoring fields rearms transient scheduling through the normal enable path.
 s->ch_events.mask=SPU_EVENT_TM;s->dec_intr_deadline=umax;s->set_interrupt_status(true);assert(s->dec_intr_armed.load());
 s->cancel_dec_interrupt();timer.armed.clear();s->ch_dec_value=800;s->ch_dec_start_timestamp=1000;clock_ticks=1000;g_cfg.core.clocks_scale=50;s->arm_dec_interrupt();
 run_timer([&](unsigned n){if(n>1)thread_ctrl::aborted=true;});assert(thread_ctrl::sleep_times[1]==21);g_cfg.core.clocks_scale=100;timer.armed.clear();
 // Alternate unsafe and safe checks: only safe calls count toward interrupt polling.
 *s=spu_thread{};s->ch_events.mask=SPU_EVENT_LR;s->set_interrupt_status(true);
 for(unsigned i=0;i<1024;i++){s->allow_interrupts_in_cpu_work=false;s->cpu_work();s->allow_interrupts_in_cpu_work=true;s->cpu_work();assert(s->state.bits&1);}
 assert(s->cpu_work_iteration_count==1024);assert(s->lr_polls==4);assert(s->polled==64);
 // Unsafe checkpoints defer dispatch; safe ones preserve the interrupted PC in SRR0.
 s->deliver=SPU_EVENT_LR;s->unsavable=true;s->allow_interrupts_in_cpu_work=false;s->check_state_with_interrupts();assert(s->pc==0x100&&s->interrupts_enabled);
 s->unsavable=false;s->branch=0x30000000|(0x240<<5);
 try{s->check_state_with_interrupts();assert(false);}catch(escaped&){}assert(s->srr0==0x100&&s->pc==0x240&&!s->interrupts_enabled&&!s->in_cpu_work);
 // Safe-block dispatcher boundaries can deliver without jumping out of a nonexistent gateway.
 *s=spu_thread{};s->ch_events.mask=SPU_EVENT_LR;s->deliver=SPU_EVENT_LR;s->set_interrupt_status(true);
 s->check_state_with_interrupts(false);assert(s->srr0==0x100&&s->pc==0&&!s->interrupts_enabled);
 assert(!s->allow_interrupts_in_cpu_work&&s->interrupt_requires_escape);
 // A timer can publish while cpu_work is about to clear pending; pending_recheck retains it.
 *s=spu_thread{};g_cfg.core.mfc_transfers_shuffling=1;s->mfc_size=2;
 s->before_pending_clear=[&]{s->state+=cpu_flag::pending+cpu_flag::pending_recheck;};s->cpu_work();assert(s->state.bits==1);
 s->before_pending_clear={};s->cpu_work();assert(s->state.bits==0);
 puts("SPU interrupts: deadlines, zero, cancellation, ID reuse, restore, clock scale, safe checkpoints, LR cadence and pending races passed.");
}

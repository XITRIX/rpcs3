#include "Emu/CPU/Backends/AArch64/SPUReservationAtomic.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <random>
using U = unsigned long long;
alignas(128) U stamp = 0;
U load() { return __atomic_load_n(&stamp, __ATOMIC_SEQ_CST); }
void store(U n) { __atomic_store_n(&stamp,n,__ATOMIC_SEQ_CST); }
int main()
{
    std::mt19937_64 random(0x5a7521);
    for (unsigned i=0;i<1000000;i++)
    {
        U initial=random(), expected=i%2?initial:random(), desired=random();
        store(initial);
        bool result=rpcs3::spu::reservation_compare_exchange(stamp,expected,desired);
        assert(result==(expected==initial));
        assert(load()==(result?desired:initial));
    }
    for (unsigned threads : {1u,2u,6u})
    {
        store(0); std::vector<std::thread> workers;
        for(unsigned i=0;i<threads;i++) workers.emplace_back([]{
            for(unsigned n=0;n<100000;n++)
                for(;;) { U old=load(); if(rpcs3::spu::reservation_compare_exchange(stamp,old,old+128)) break; }
        });
        for(auto& t:workers)t.join();
        assert(load()==128ull*100000*threads);
    }
    store(0); U payload=0;
    std::thread producer([&]{
        for(U n=1;n<=100000;n++) {
            while(load()!=0){} payload=n;
            assert(rpcs3::spu::reservation_compare_exchange(stamp,U{0},U{1}));
        }
    });
    for(U n=1;n<=100000;n++) {
        while(load()!=1){} assert(payload==n);
        assert(rpcs3::spu::reservation_compare_exchange(stamp,U{1},U{0}));
    }
    producer.join();
    std::puts("SPU reservation CAS: 1M differential values, 900k concurrent commits, 100k ordered publications passed");
}

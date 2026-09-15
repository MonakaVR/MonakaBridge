#include "helpers.hpp"
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <new>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
static std::size_t allocations=0,bytesAllocated=0;
void* operator new(std::size_t n){++allocations;bytesAllocated+=n;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
double cpu(){
#ifdef _WIN32
 FILETIME c,e,k,u;CHECK(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u));ULARGE_INTEGER a,b;a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return double(a.QuadPart+b.QuadPart)*100;
#else
 return double(std::clock())*1e9/CLOCKS_PER_SEC;
#endif
}
int main()try{
 constexpr int N=10000;auto c=config();mb::Bridge bridge(c,SB);std::vector<std::string> inputs;inputs.reserve(N+200);
 for(int i=0;i<N+200;++i){auto p=sample(i%2?"pico":"other",i);p.timestamp_ns+=std::int64_t(i)*10000000;p.sent_at_ns=p.timestamp_ns+1000000;inputs.push_back(wire(p));}
 std::size_t payloadBytes=0;auto sink=[&](auto,std::string_view b){payloadBytes+=b.size();return true;};
 for(int i=0;i<200;++i){bridge.receive(inputs[i],i%2?"a":"b",10000000LL*(i+1));bridge.fanout.flush(sink,10000000LL*(i+1));}
 std::vector<double> times(N);payloadBytes=0;allocations=bytesAllocated=0;auto cpuStart=cpu();auto start=std::chrono::steady_clock::now();
 for(int i=0;i<N;++i){auto t=std::chrono::steady_clock::now();bridge.receive(inputs[i+200],i%2?"a":"b",10000000LL*(i+201));bridge.fanout.flush(sink,10000000LL*(i+201));times[i]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count();}
 double wall=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count(),cpuElapsed=cpu()-cpuStart;auto count=allocations,allocated=bytesAllocated;std::sort(times.begin(),times.end());
 CHECK(bridge.fanout.errors==0);for(auto ch:{mb::Channel::Mirror,mb::Channel::Monaka,mb::Channel::Steamvr})CHECK(bridge.fanout.pending(ch)==0);
 std::cout<<"{\"samples\":"<<N<<",\"sources\":2,\"outputs\":3,\"measurement\":\"fixed C1 decode to serialized egress callback; excludes OS scheduling and hardware\",\"process_cpu_ns_per_observation\":"<<cpuElapsed/N<<",\"process_cpu_percent_one_core\":"<<100*cpuElapsed/wall<<",\"allocations_per_observation\":"<<double(count)/N<<",\"allocated_bytes_per_observation\":"<<double(allocated)/N<<",\"egress_bytes\":"<<payloadBytes<<",\"latency_us_p50\":"<<times[N/2]<<",\"latency_us_p95\":"<<times[N*95/100]<<",\"latency_us_p99\":"<<times[N*99/100]<<",\"queue_capacity_per_channel\":512,\"pending_after_flush\":0}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

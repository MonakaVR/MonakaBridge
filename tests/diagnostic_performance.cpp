#include "helpers.hpp"
#include "monaka_bridge/diagnostic.hpp"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
using J=nlohmann::json;
int main(int argc,char** argv){const int n=10000;J result=J::object();for(const auto mode:{"off_active","on_idle","on_active"}){
 std::atomic<int> events{0};std::unique_ptr<mb::DiagnosticWriter> writer;std::unique_ptr<mb::DiagnosticObserver> observer;
 std::ofstream file;if(argc>1)file.open(std::string(argv[1])+"."+mode+".jsonl");
 mb::Bridge bridge(config(),SB);if(std::string(mode)!="off_active"){writer=std::make_unique<mb::DiagnosticWriter>([&](const J& e){++events;if(file){file<<e.dump()<<'\n';file.flush();}},2048);observer=std::make_unique<mb::DiagnosticObserver>(*writer);bridge.registry.diagnostic=observer.get();}
 auto p=sample();auto start=std::chrono::steady_clock::now();auto cpu=std::clock();std::int64_t maximum=0;
 for(int i=0;i<n;++i){auto lap=std::chrono::steady_clock::now();if(std::string(mode)=="on_idle")bridge.tick(i*1000000LL);else{p.sequence=i;p.timestamp_ns+=1000000;p.sent_at_ns=p.timestamp_ns;bridge.receive(wire(p),"mock",i*1000000LL);}
 maximum=std::max(maximum,std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-lap).count());}
 double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();double producerCpu=double(std::clock()-cpu)/CLOCKS_PER_SEC;
 if(writer)writer->close();J metrics={{"iterations",n},{"producer_wall_s",seconds},{"process_cpu_s",producerCpu},{"producer_max_ns",maximum},{"events",events.load()},{"event_rate_hz",events/seconds},{"dropped_events",writer?writer->dropped():0},{"critical_dropped_events",writer?writer->criticalDropped():0},{"sink","JSONL file flush per event"}};
#ifdef _WIN32
 PROCESS_MEMORY_COUNTERS memory{};GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));metrics["working_set_bytes"]=memory.WorkingSetSize;
#endif
 result[mode]=metrics;}
 std::cout<<result.dump(2)<<'\n';return 0;}

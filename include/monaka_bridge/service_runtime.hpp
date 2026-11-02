#pragma once
#include "monaka_bridge/bridge.hpp"
#include "monaka_bridge/network.hpp"
#include "monaka_bridge/health.hpp"
#include "monaka_bridge/discovery.hpp"
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

namespace mb {
// One product loop shared by the existing headless/tray-child CLI and the
// native owner. Ownership policy stays outside normalization and fanout.
inline void runService(const std::filesystem::path& path,const std::uint16_t (&ports)[4],
                       std::int64_t duration,const std::function<bool()>& stopping,
                       const std::function<void()>& ready) {
 auto config=loadConfig(path);
 Udp input(ports[0]);Udp output;const auto epoch=monotonicNs();Bridge bridge(config,uuid());DiscoveryCache discovery;
 std::unique_ptr<HealthWriter> healthWriter;
 try{auto health=path;health+=L".status.json";healthWriter=std::make_unique<HealthWriter>(health);}catch(...){/* Diagnostic failure cannot disable tracking. */}
 auto modified=std::filesystem::last_write_time(path);std::int64_t lastStatus=-1000000000;
 ready();
 while(!stopping()) {
  auto now=monotonicNs()-epoch;if(duration>0&&now>=duration)break;
  for(int i=0;i<128;++i){auto d=input.receive();if(!d)break;discovery.receive(bridge,d->bytes,d->peer,monotonicNs()-epoch);}
  now=monotonicNs()-epoch;bridge.tick(now);
  bridge.fanout.flush([&](Channel c,std::string_view bytes){return output.send(ports[c==Channel::Monaka?1:c==Channel::Steamvr?2:3],bytes);},now);
  if(now-lastStatus>=500000000) {
   lastStatus=now;
   try{auto change=std::filesystem::last_write_time(path);if(change!=modified){bridge.reconfigure(loadConfig(path),now);modified=change;}}catch(const std::exception& e){std::cerr<<"config not applied: "<<e.what()<<'\n';}
   if(healthWriter)try{
    const auto unixMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto snapshot=healthSnapshot(bridge,now,unixMs);snapshot["discovery"]=discovery.snapshot(bridge,now);
    snapshot["health_write_failures"]=healthWriter->writeFailures();snapshot["health_replace_failures"]=healthWriter->replaceFailures();snapshot["health_dropped"]=healthWriter->dropped();healthWriter->publish(std::move(snapshot));
   }catch(...){/* Snapshot allocation/format failure is best effort. */}
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
}
int nativeOwner(int argc,char** argv);
int nativeOwnerCommand(int argc,char** argv);
}

#include "monaka_bridge/runtime_replay.hpp"
#include "monaka_bridge/bridge.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
namespace mb {
using J=nlohmann::json;
namespace {
J jsonWire(const c1::Envelope& e){std::string bytes;c1::Error error;if(!c1::EncodeEnvelope(e,bytes,error))throw std::runtime_error(error.message);return J::parse(bytes);}
}
int runtimeReplay(int argc,char** argv){
 // File mode creates no sockets, shared stop events, device clients or output driver.
 if(argc!=6&&argc!=7)throw std::invalid_argument("--replay CONFIG INPUT_JSONL OUTPUT_JSONL SESSION [--mtp]");
 const bool mtp=argc==7&&std::string(argv[6])=="--mtp";if(argc==7&&!mtp)throw std::invalid_argument("invalid replay mode");
 const auto config=loadConfig(argv[2]);Bridge bridge(config,argv[5]);
 if(std::filesystem::exists(argv[4]))throw std::invalid_argument("Refuse existing replay output");
 std::ifstream input(argv[3]);std::ofstream output(argv[4],std::ios::out|std::ios::trunc);if(!input||!output)throw std::runtime_error("replay files unavailable");
 J pairs=J::array();bridge.normalization().observer=[&](const char* stage,const c1::Envelope& before,const c1::Envelope& after){pairs.push_back({{"stage",stage},{"before",jsonWire(before)},{"after",jsonWire(after)}});};
 std::string line;std::size_t processed=0;
 while(std::getline(input,line)){
  if(line.empty())continue;if(line.size()>65536)throw std::invalid_argument("replay carrier exceeds bound");
  const auto row=J::parse(line);const auto now=row.at("received_at_ns").get<std::int64_t>();if(now<0)throw std::invalid_argument("negative receipt");
  pairs=J::array();J result={{"received_at_ns",now},{"peer",row.value("peer","file-replay")},{"emitted",J::array()}};
  try{
   const auto bytes=row.at("envelope").dump();
   if(mtp){c1::Envelope e;c1::Error error;if(!c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),e,error))throw std::invalid_argument(error.message);result["emitted"].push_back(jsonWire(bridge.normalizeMtp(e)));result["accepted"]=true;}
   else {result["accepted"]=bridge.receive(bytes,row.value("peer","file-replay"),now);bridge.fanout.flush([&](Channel channel,std::string_view wire){if(channel==Channel::Monaka)result["emitted"].push_back(J::parse(wire));return true;},now);}
  }catch(const std::invalid_argument& e){result["accepted"]=false;result["reject_reason"]=e.what();}
  result["normalization_pairs"]=pairs;result["diagnostics"]=J::parse(bridge.normalization().diagnostics());
  output<<result.dump()<<'\n';if(!output)throw std::runtime_error("replay output failed");++processed;
 }
 if(!input.eof())throw std::runtime_error("replay read failed");
 std::cout<<J{{"processed",processed},{"runtime_path","Bridge::receive/Registry -> canonical P1 -> calibrate -> Bridge::publish/canonical P1 -> Fanout"},{"normalization",J::parse(bridge.normalization().diagnostics())}}.dump()<<'\n';return 0;
}
}

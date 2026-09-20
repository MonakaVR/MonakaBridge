#pragma once
#include "direct.hpp"
namespace mb::detail {
// Legacy CLI shorthand is safe only when unambiguous within this publisher.
inline const Binding& uniqueTracker(const Config& config,const std::string& tracker){
 const Binding* selected=nullptr;
 for(const auto& [key,b]:config.bindings)if(b.tracker==tracker){
  if(selected)throw std::invalid_argument("ambiguous tracker across sources; edit the exact source/device mapping in configuration");
  selected=&b;
 }
 if(!selected)throw std::invalid_argument("tracker not mapped");return *selected;
}
inline const Binding& runtimeTracker(const Config& config,const std::string& serial){
 for(const auto& [key,b]:config.bindings)
  if(runtimeSerial(outputSource(config.bridgeId,b.source),b.tracker)==serial)return b;
 throw std::invalid_argument("select a mapped Monaka Direct serial for this publisher/source/tracker");
}
}

#pragma once
#include "config.hpp"
#include <deque>
namespace mb {
// Runtime provisioning/lifecycle wrapper. All declaration semantics are P1 MBIP.
class RuntimeNormalization {
 Config config_;
 std::map<Key,CapabilityProfile> lifetimeContracts_;
 std::string session_;
 std::deque<std::string> recent_;
 std::uint64_t accepted_=0,rejected_=0;
 c1::Envelope apply(const c1::Envelope&,const CapabilityProfile&,const monaka::bridge_input::Binding&,monaka::bridge_input::Mode,const char* stage);
public:
 using Observer=std::function<void(const char*,const c1::Envelope&,const c1::Envelope&)>;
 Observer observer; // Empty in live service; explicit replay-only full-field evidence.
 RuntimeNormalization(Config config,std::string session):config_(std::move(config)),lifetimeContracts_(config_.contracts),session_(std::move(session)){}
 bool provisioned(const c1::Envelope&);
 c1::Envelope input(const c1::Envelope&);
 c1::Envelope output(const c1::Envelope&,bool generatedStatic=false);
 void validateReconfigure(const Config&)const;
 void reconfigure(Config config){validateReconfigure(config);for(const auto& [key,value]:config.contracts)lifetimeContracts_.emplace(key,value);config_=std::move(config);}
 std::string diagnostics()const;
};
}

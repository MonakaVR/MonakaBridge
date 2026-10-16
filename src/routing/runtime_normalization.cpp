#include "monaka_bridge/runtime_normalization.hpp"
#include <nlohmann/json.hpp>
namespace mb {
namespace mi=monaka::input;namespace bp=monaka::bridge_input;using J=nlohmann::json;
namespace {const char* modeName(bp::Mode mode){return mode==bp::Mode::STRICT?"STRICT":"LEGACY_DYNAMIC";}}
c1::Envelope RuntimeNormalization::apply(const c1::Envelope& message,const CapabilityProfile& profile,const bp::Binding& binding,bp::Mode mode,const char* stage){
 const auto facts=mi::ProjectCapabilities(message);
 J record={{"stage",stage},{"policy",modeName(mode)},{"profile",profile.id},{"family",facts.identity.family},
  {"publisher",facts.identity.publisher},{"source",facts.identity.source},{"entity",facts.identity.entity},
  {"session",facts.session},{"advertised",facts.capabilities.tokens()},{"declaration",binding.declaration.tokens()}};
 try{
  auto result=bp::Normalizer(binding,mode).normalize(message);
  record["normalized"]=result.source.capabilities.tokens();record["result"]="accepted";
  record["modified"]=std::visit([](const auto& m){if constexpr(std::is_same_v<std::decay_t<decltype(m)>,c1::TrackerObservation>||std::is_same_v<std::decay_t<decltype(m)>,c1::ObservationDeviceState>||std::is_same_v<std::decay_t<decltype(m)>,c1::MtpPose>||std::is_same_v<std::decay_t<decltype(m)>,c1::MtpTrackerState>)return m.capabilities;else return std::vector<std::string>{};},message)!=result.source.capabilities.tokens();
  ++accepted_;recent_.push_back(record.dump());if(recent_.size()>64)recent_.pop_front();
  if(observer)observer(stage,message,result.message);
  return std::move(result.message);
 }catch(const mi::ContractError& e){++rejected_;record["result"]="rejected";record["reason"]=e.code;recent_.push_back(record.dump());if(recent_.size()>64)recent_.pop_front();throw;}
}
bool RuntimeNormalization::provisioned(const c1::Envelope& message){
 const auto facts=mi::ProjectCapabilities(message);auto key=Key{facts.identity.source,facts.identity.entity};
 if(config_.bindings.count(key)&&config_.contracts.count(key))return true;
 ++rejected_;J record={{"stage","input_authorization"},{"policy","STRICT"},{"result","rejected"},{"reason","MissingDeclaration"},{"source",key.first},{"entity",key.second}};
 recent_.push_back(record.dump());if(recent_.size()>64)recent_.pop_front();return false;
}
c1::Envelope RuntimeNormalization::input(const c1::Envelope& message){
 const auto facts=mi::ProjectCapabilities(message);const auto key=Key{facts.identity.source,facts.identity.entity};
 const auto& profile=config_.contracts.at(key);
 bp::Binding binding{{"monaka.observation","",key.first,key.second},facts.session,mi::CapabilitySet(profile.inputDeclaration),profile.provenance};
 return apply(message,profile,binding,profile.inputMode,"input_after_authorization");
}
c1::Envelope RuntimeNormalization::output(const c1::Envelope& message,bool preserveSupportedSubset){
 const auto facts=mi::ProjectCapabilities(message);
 for(const auto& [key,mapping]:config_.bindings)if(mapping.source==facts.identity.source&&mapping.tracker==facts.identity.entity){
  const auto found=config_.contracts.find(key);if(found==config_.contracts.end())break;
  const auto& profile=found->second;
  if(const auto* pose=std::get_if<c1::MtpPose>(&message);pose&&(pose->input.source_id!=mapping.source||pose->input.device_id!=mapping.device))throw mi::ContractError("IdentityMismatch");
  const mi::CapabilitySet allowed(profile.outputDeclaration);
  const auto declaration=preserveSupportedSubset?facts.capabilities:allowed;
  if(preserveSupportedSubset&&!declaration.subsetOf(allowed))throw mi::ContractError("UndeclaredCapability");
  bp::Binding binding{{"monaka.tracking",config_.bridgeId,mapping.source,mapping.tracker},session_,declaration,profile.provenance};
  return apply(message,profile,binding,profile.outputMode,"output_before_publication");
 }
 ++rejected_;recent_.push_back(J{{"stage","output_before_publication"},{"policy","STRICT"},{"result","rejected"},{"reason","MissingDeclaration"},{"source",facts.identity.source},{"publisher",facts.identity.publisher},{"entity",facts.identity.entity},{"session",facts.session},{"advertised",facts.capabilities.tokens()}}.dump());if(recent_.size()>64)recent_.pop_front();throw mi::ContractError("MissingDeclaration");
}
void RuntimeNormalization::validateReconfigure(const Config& next)const{
 for(const auto& [key,p]:lifetimeContracts_){auto i=next.contracts.find(key);if(i==next.contracts.end())continue;const auto& q=i->second;
  if(p.inputMode!=q.inputMode||p.outputMode!=q.outputMode||mi::CapabilitySet(p.inputDeclaration)!=mi::CapabilitySet(q.inputDeclaration)||mi::CapabilitySet(p.outputDeclaration)!=mi::CapabilitySet(q.outputDeclaration))throw std::invalid_argument("capability contract change requires coordinated new session/restart");
 }
}
std::string RuntimeNormalization::diagnostics()const{J records=J::array();for(const auto& s:recent_)records.push_back(J::parse(s));return J{{"canonical_mbip","3ea3194479cfa1a04b0fcbcf04916186efb4f9d3"},{"accepted",accepted_},{"rejected",rejected_},{"recent",records}}.dump();}
}

#include "monaka_bridge/config.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <limits>
#include <chrono>
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#undef STRICT // Windows handle macro must not shadow the pinned P1 Mode::STRICT.
#endif
namespace mb {
using J=nlohmann::json;
Policy parsePolicy(const std::string& s){if(s=="steamvr")return Policy::Steamvr;if(s=="monaka")return Policy::Monaka;if(s=="both")return Policy::Both;if(s=="disabled")return Policy::Disabled;throw std::invalid_argument("unknown output policy");}
std::string policyName(Policy p){switch(p){case Policy::Steamvr:return "steamvr";case Policy::Monaka:return "monaka";case Policy::Both:return "both";default:return "disabled";}}
namespace {
monaka::bridge_input::Mode capabilityMode(const std::string& value){if(value=="STRICT")return monaka::bridge_input::Mode::STRICT;if(value=="LEGACY_DYNAMIC")return monaka::bridge_input::Mode::LEGACY_DYNAMIC;throw std::invalid_argument("unknown normalization policy");}
std::string capabilityModeName(monaka::bridge_input::Mode m){return m==monaka::bridge_input::Mode::STRICT?"STRICT":"LEGACY_DYNAMIC";}
CapabilityProfile capabilityProfile(const J& j){CapabilityProfile p;p.id=j.at("id");p.provenance=j.at("provenance");p.inputDeclaration=j.at("input_declaration").get<std::vector<std::string>>();p.outputDeclaration=j.at("output_declaration").get<std::vector<std::string>>();p.inputMode=capabilityMode(j.value("input_policy","STRICT"));p.outputMode=capabilityMode(j.value("output_policy","STRICT"));return p;}
J capabilityJson(const CapabilityProfile& p){return J{{"id",p.id},{"provenance",p.provenance},{"input_declaration",p.inputDeclaration},{"output_declaration",p.outputDeclaration},{"input_policy",capabilityModeName(p.inputMode)},{"output_policy",capabilityModeName(p.outputMode)}};}
J read(const std::filesystem::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("cannot read configuration");J j;f>>j;return j;}
std::uint32_t u32(const J& j){if(!j.is_number_integer()||j<0||j>UINT32_MAX)throw std::invalid_argument("revision out of range");return j.get<std::uint32_t>();}
Rigid rigid(const J& j){Rigid r;r.rotation=j.at("rotation").get<Quat>();r.translation=j.at("translation").get<Vec>();return r;}
J toJson(const Rigid& r){return J{{"rotation",r.rotation},{"translation",r.translation}};}
void id(const std::string& id){c1::MtpTrackerState s;s.version={2,0};s.modality="none";s.publisher_id="validation";s.source_id=id;s.tracker_id="check";s.session_id=s.clock_id="00000000-0000-4000-8000-000000000001";s.timestamp_kind="receive";s.presence="unknown";s.tracking_state="unknown";s.coordinate_space={"world","rh_y_up_neg_z_forward",0};std::string b;c1::Error e;if(!c1::EncodeEnvelope(s,b,e))throw std::invalid_argument("invalid ID: "+e.message);}
template<std::size_t N>void axes(const std::array<int,N>& a){std::set<int>s;for(int x:a){if(x==0||x>int(N)||x<-int(N))throw std::invalid_argument("invalid profile permutation");s.insert(std::abs(x));}if(s.size()!=N)throw std::invalid_argument("duplicate profile axis");}
void transform(const Rigid& r){for(double v:r.translation)if(!std::isfinite(v))throw std::invalid_argument("nonfinite translation");auto q=normalized(r.rotation);double n=0;for(auto x:r.rotation)n+=x*x;if(std::abs(n-1)>1e-5)throw std::invalid_argument("configuration rotations must be unit quaternions");}
void backup(const std::filesystem::path& p){if(!std::filesystem::exists(p))return;auto b=p;b+=".pre-monaka-bridge.bak";if(!std::filesystem::exists(b))std::filesystem::copy_file(p,b);}
}
std::vector<std::string> projectOutputCapabilities(const std::vector<std::string>& source,const CapabilityProfile& contract,const Binding& binding){
 const auto contains=[&](const std::string& value){return std::find(source.begin(),source.end(),value)!=source.end();};
 const bool orientation=contains("orientation"),offset=norm(binding.mount.translation)>0;
 std::vector<std::string> result;
 for(const auto& capability:contract.outputDeclaration){
  bool supported=false;
  if(capability=="position"||capability=="orientation")supported=contains(capability);
  else if(capability=="linear_velocity")supported=contains("position")&&contains(capability)&&(!offset||orientation);
  else if(capability=="angular_velocity")supported=orientation; // Existing orientation derivative path.
  else if(capability=="linear_acceleration")supported=contains("position")&&contains(capability)&&!offset;
  else if(capability=="battery_fraction"||capability=="charging")supported=contains(capability);
  if(supported)result.push_back(capability);
 }
 return result;
}
void validateConfig(const Config& c){
 id(c.bridgeId);if(c.bindings.size()>256||c.profiles.size()>64||c.timeoutNs<1000000||c.timeoutNs>10000000000LL)throw std::invalid_argument("config bounds");
 std::set<Key> trackers;
 for(auto& [name,p]:c.profiles){id(name);id(p.convention);axes(p.positionAxes);axes(p.quaternionAxes); // Legacy evidence is descriptive; it is never a runtime-use requirement.}
 for(auto& [key,b]:c.bindings){
  auto contract=c.contracts.find(key);
  if(contract!=c.contracts.end()){
   if(contract->second.id.empty()||contract->second.provenance.empty())throw std::invalid_argument("MissingDeclaration: profile ID/provenance required");
   monaka::input::CapabilitySet input(contract->second.inputDeclaration),output(contract->second.outputDeclaration);
   if(monaka::input::CapabilitySet(projectOutputCapabilities(input.tokens(),contract->second,b))!=output)
    throw std::invalid_argument("output declaration exceeds verified backend/transform support");
  } // Unprovisioned mappings remain subject to all ordinary configuration checks.
  if(key!=Key{b.source,b.device})throw std::invalid_argument("mapping key mismatch");
  for(const auto& v:{b.source,b.device,b.tracker,b.profile,b.inputSpace,b.worldSpace})id(v);
  if(!trackers.insert(Key{b.source,b.tracker}).second)throw std::invalid_argument("logical tracker collision");transform(b.world);transform(b.mount);
 }
}
Config parseConfigDocument(const std::string& bytes){
 auto j=J::parse(bytes);if(j.at("namespace")!="monaka.bridge"||j.at("version")!=2)throw std::invalid_argument("unsupported config namespace/version");
 Config c;c.bridgeId=j.at("bridge_id");c.revision=u32(j.at("mapping_revision"));c.policy=parsePolicy(j.at("policy"));auto timeout=j.value("pose_timeout_ms",J(500));if(!timeout.is_number_integer()||timeout<1||timeout>10000)throw std::invalid_argument("timeout out of range");c.timeoutNs=timeout.get<std::int64_t>()*1000000;
 for(auto it=j.at("profiles").begin();it!=j.at("profiles").end();++it){const auto& v=it.value();Profile p;p.convention=v.at("convention");p.positionAxes=v.at("position_axes").get<std::array<int,3>>();p.quaternionAxes=v.at("quaternion_axes").get<std::array<int,4>>();p.approved=v.value("approved",false);p.evidence=v.value("evidence","");p.angularSpaceVerified=v.value("angular_space_verified",false);c.profiles.emplace(it.key(),p);}
 for(const auto& v:j.at("mappings")){Binding b;b.source=v.at("source_id");b.device=v.at("device_id");b.tracker=v.at("tracker_id");b.profile=v.at("profile");b.inputSpace=v.at("input_space");b.inputRevision=u32(v.at("input_revision"));b.worldSpace=v.at("world_space");b.worldRevision=u32(v.at("world_revision"));b.spaceApproved=v.value("space_approved",false);if(v.contains("world"))b.world=rigid(v.at("world"));if(v.contains("mount"))b.mount=rigid(v.at("mount"));if(v.contains("runtime_contract"))c.contracts.emplace(Key{b.source,b.device},capabilityProfile(v.at("runtime_contract")));if(!c.bindings.emplace(Key{b.source,b.device},b).second)throw std::invalid_argument("source/device collision");}
 validateConfig(c);c.sourceDocument=bytes;return c;
}
Config loadConfig(const std::filesystem::path& path){auto c=parseConfigDocument(readConfigBytes(path));c.sourcePath=std::filesystem::weakly_canonical(path);return c;}
void saveConfig(const std::filesystem::path& path,const Config& c){
 validateConfig(c);J j={{"namespace","monaka.bridge"},{"version",2},{"bridge_id",c.bridgeId},{"mapping_revision",c.revision},{"policy",policyName(c.policy)},{"pose_timeout_ms",c.timeoutNs/1000000},{"profiles",J::object()},{"mappings",J::array()}};
 for(auto& [n,p]:c.profiles)j["profiles"][n]={{"convention",p.convention},{"position_axes",p.positionAxes},{"quaternion_axes",p.quaternionAxes},{"approved",p.approved},{"evidence",p.evidence},{"angular_space_verified",p.angularSpaceVerified}};
 for(auto& [k,b]:c.bindings){j["mappings"].push_back({{"source_id",b.source},{"device_id",b.device},{"tracker_id",b.tracker},{"profile",b.profile},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},{"world_space",b.worldSpace},{"world_revision",b.worldRevision},{"space_approved",b.spaceApproved},{"world",toJson(b.world)},{"mount",toJson(b.mount)}});if(c.contracts.count(k))j["mappings"].back()["runtime_contract"]=capabilityJson(c.contracts.at(k));}
 // Preserve unknown document members while updating known typed fields.
 if(!c.sourceDocument.empty()){
  auto original=J::parse(c.sourceDocument);auto known=j;
  const auto merge=[](auto&& self,J& base,const J& delta)->void{if(base.is_object()&&delta.is_object()){for(auto it=delta.begin();it!=delta.end();++it)self(self,base[it.key()],it.value());}else base=delta;};
  for(auto it=known["profiles"].begin();it!=known["profiles"].end();++it){J row=original.value("profiles",J::object()).value(it.key(),J::object());merge(merge,row,it.value());j["profiles"][it.key()]=row;}
  for(std::size_t i=0;i<known["mappings"].size();++i){const auto& delta=known["mappings"][i];for(const auto& old:original.value("mappings",J::array()))if(old.value("source_id","")==delta.at("source_id")&&old.value("device_id","")==delta.at("device_id")){J row=old;merge(merge,row,delta);if(!delta.contains("runtime_contract"))row.erase("runtime_contract");j["mappings"][i]=row;break;}}
  for(auto it=original.begin();it!=original.end();++it)if(!j.contains(it.key()))j[it.key()]=it.value();
 }
 const auto bytes=j.dump(2)+'\n';const auto canonical=std::filesystem::weakly_canonical(path);
 const bool same=!c.sourcePath.empty()&&canonical==c.sourcePath;
 commitConfigDocument(path,same?std::optional<std::string>(c.sourceDocument):std::nullopt,bytes);
 // The loaded snapshot stays immutable; a subsequent edit must reload.
}
void migrateLegacy(const std::filesystem::path& route,const std::filesystem::path& alignment,const std::filesystem::path& destination,Config c){
 if(std::filesystem::exists(destination))throw std::invalid_argument("migration destination already exists");
 std::ifstream f(route);std::string value;f>>value;if(!f)throw std::runtime_error("missing legacy route");
 if(value=="steamvr-direct")value="steamvr";if(value=="monaka-external")value="monaka";c.policy=parsePolicy(value);
 auto a=read(alignment);if(a.at("version")!=1)throw std::invalid_argument("unknown legacy alignment version");Vec t{a.at("xMeters"),a.at("yMeters"),a.at("zMeters")};
 if(c.bindings.empty())throw std::invalid_argument("explicit destination spaces required");
 for(auto& [k,b]:c.bindings){b.world.translation=t;b.spaceApproved=false;}
 if(c.revision==UINT32_MAX)throw std::invalid_argument("revision exhausted");++c.revision;validateConfig(c);backup(route);backup(alignment);saveConfig(destination,c);
}
std::string runtimeSerial(const std::string& source,const std::string& tracker){
 const auto hex=[](const std::string& x){static const char* h="0123456789abcdef";std::string r;for(unsigned char c:x){r+=h[c>>4];r+=h[c&15];}return r;};
 return "monaka-direct:"+hex(source)+":"+hex(tracker);
}
}

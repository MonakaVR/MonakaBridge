#include "monaka_bridge/config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <limits>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace mb {
using J=nlohmann::json;
Policy parsePolicy(const std::string& s){if(s=="steamvr")return Policy::Steamvr;if(s=="monaka")return Policy::Monaka;if(s=="both")return Policy::Both;if(s=="disabled")return Policy::Disabled;throw std::invalid_argument("unknown output policy");}
std::string policyName(Policy p){switch(p){case Policy::Steamvr:return "steamvr";case Policy::Monaka:return "monaka";case Policy::Both:return "both";default:return "disabled";}}
namespace {
J read(const std::filesystem::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("cannot read configuration");J j;f>>j;return j;}
std::uint32_t u32(const J& j){if(!j.is_number_integer()||j<0||j>UINT32_MAX)throw std::invalid_argument("revision out of range");return j.get<std::uint32_t>();}
Rigid rigid(const J& j){Rigid r;r.rotation=j.at("rotation").get<Quat>();r.translation=j.at("translation").get<Vec>();return r;}
J toJson(const Rigid& r){return J{{"rotation",r.rotation},{"translation",r.translation}};}
void id(const std::string& id){c1::MtpTrackerState s;s.version={2,0};s.modality="none";s.publisher_id="validation";s.source_id=id;s.tracker_id="check";s.session_id=s.clock_id="00000000-0000-4000-8000-000000000001";s.timestamp_kind="receive";s.presence="unknown";s.tracking_state="unknown";s.coordinate_space={"world","rh_y_up_neg_z_forward",0};std::string b;c1::Error e;if(!c1::EncodeEnvelope(s,b,e))throw std::invalid_argument("invalid ID: "+e.message);}
template<std::size_t N>void axes(const std::array<int,N>& a){std::set<int>s;for(int x:a){if(x==0||x>int(N)||x<-int(N))throw std::invalid_argument("invalid profile permutation");s.insert(std::abs(x));}if(s.size()!=N)throw std::invalid_argument("duplicate profile axis");}
void transform(const Rigid& r){for(double v:r.translation)if(!std::isfinite(v))throw std::invalid_argument("nonfinite translation");auto q=normalized(r.rotation);double n=0;for(auto x:r.rotation)n+=x*x;if(std::abs(n-1)>1e-5)throw std::invalid_argument("configuration rotations must be unit quaternions");}
void backup(const std::filesystem::path& p){if(!std::filesystem::exists(p))return;auto b=p;b+=".pre-monaka-bridge.bak";if(!std::filesystem::exists(b))std::filesystem::copy_file(p,b);}
}
void validateConfig(const Config& c){
 id(c.bridgeId);if(c.bindings.size()>256||c.profiles.size()>64||c.timeoutNs<1000000||c.timeoutNs>10000000000LL)throw std::invalid_argument("config bounds");
 std::set<Key> trackers;
 for(auto& [name,p]:c.profiles){id(name);id(p.convention);axes(p.positionAxes);axes(p.quaternionAxes);if(p.approved&&p.evidence.empty())throw std::invalid_argument("approved profile needs evidence");}
 for(auto& [key,b]:c.bindings){
  if(key!=Key{b.source,b.device})throw std::invalid_argument("mapping key mismatch");
  for(const auto& v:{b.source,b.device,b.tracker,b.profile,b.inputSpace,b.worldSpace})id(v);
  if(!trackers.insert(Key{b.source,b.tracker}).second)throw std::invalid_argument("logical tracker collision");transform(b.world);transform(b.mount);
 }
}
Config loadConfig(const std::filesystem::path& path){
 auto j=read(path);if(j.at("namespace")!="monaka.bridge"||j.at("version")!=2)throw std::invalid_argument("unsupported config namespace/version");
 Config c;c.bridgeId=j.at("bridge_id");c.revision=u32(j.at("mapping_revision"));c.policy=parsePolicy(j.at("policy"));auto timeout=j.value("pose_timeout_ms",J(500));if(!timeout.is_number_integer()||timeout<1||timeout>10000)throw std::invalid_argument("timeout out of range");c.timeoutNs=timeout.get<std::int64_t>()*1000000;
 for(auto it=j.at("profiles").begin();it!=j.at("profiles").end();++it){const auto& v=it.value();Profile p;p.convention=v.at("convention");p.positionAxes=v.at("position_axes").get<std::array<int,3>>();p.quaternionAxes=v.at("quaternion_axes").get<std::array<int,4>>();p.approved=v.at("approved");p.evidence=v.value("evidence","");p.angularSpaceVerified=v.value("angular_space_verified",false);c.profiles.emplace(it.key(),p);}
 for(const auto& v:j.at("mappings")){Binding b;b.source=v.at("source_id");b.device=v.at("device_id");b.tracker=v.at("tracker_id");b.profile=v.at("profile");b.inputSpace=v.at("input_space");b.inputRevision=u32(v.at("input_revision"));b.worldSpace=v.at("world_space");b.worldRevision=u32(v.at("world_revision"));b.spaceApproved=v.at("space_approved");b.world=rigid(v.at("world"));b.mount=rigid(v.at("mount"));if(!c.bindings.emplace(Key{b.source,b.device},b).second)throw std::invalid_argument("source/device collision");}
 validateConfig(c);return c;
}
void saveConfig(const std::filesystem::path& path,const Config& c){
 validateConfig(c);J j={{"namespace","monaka.bridge"},{"version",2},{"bridge_id",c.bridgeId},{"mapping_revision",c.revision},{"policy",policyName(c.policy)},{"pose_timeout_ms",c.timeoutNs/1000000},{"profiles",J::object()},{"mappings",J::array()}};
 for(auto& [n,p]:c.profiles)j["profiles"][n]={{"convention",p.convention},{"position_axes",p.positionAxes},{"quaternion_axes",p.quaternionAxes},{"approved",p.approved},{"evidence",p.evidence},{"angular_space_verified",p.angularSpaceVerified}};
 for(auto& [k,b]:c.bindings)j["mappings"].push_back({{"source_id",b.source},{"device_id",b.device},{"tracker_id",b.tracker},{"profile",b.profile},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},{"world_space",b.worldSpace},{"world_revision",b.worldRevision},{"space_approved",b.spaceApproved},{"world",toJson(b.world)},{"mount",toJson(b.mount)}});
 backup(path);auto pending=path;pending+=".pending-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 {std::ofstream out(pending,std::ios::binary|std::ios::trunc);out<<j.dump(2)<<'\n';out.flush();if(!out)throw std::runtime_error("config write failed");}
 // Atomic file replacement prevents the running service from admitting a partial edit.
#ifdef _WIN32
 if(!MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("config replacement failed; pending file retained");
#else
 std::filesystem::rename(pending,path);
#endif
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

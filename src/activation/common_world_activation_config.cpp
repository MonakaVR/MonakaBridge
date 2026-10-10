#include "monaka_bridge/common_world_activation_config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <stdexcept>

namespace monaka_bridge {
namespace {
void require(bool ok,const char* why) { if(!ok) throw std::invalid_argument(why); }
bool id(const std::string& s) {
 // Validate UTF-8 separately through the strict JSON parser. Id contract is byte bounded.
 if(s.empty() || s.size()>96 || s.find_first_not_of(" \t\r\n")==std::string::npos) return false;
 for(unsigned char c:s) if(c<32 || c==127) return false;
 for(std::size_t i=0;i+1<s.size();++i) if(static_cast<unsigned char>(s[i])==0xc2 &&
  static_cast<unsigned char>(s[i+1])>=0x80 && static_cast<unsigned char>(s[i+1])<=0x9f) return false;
 return true;
}
void endpoint(CommonWorldActivationConfig& c) {
 if(c.mode==CommonWorldExecutionMode::Test) {
  require(c.transport=="deterministic" && c.endpoint=="test://sink","test registration requires deterministic test://sink; production endpoints forbidden");
  return;
 }
 require(c.transport=="udp-ipv4","unsupported production transport");
 const std::string prefix="udp://";
 require(c.endpoint.compare(0,prefix.size(),prefix)==0,"literal udp://IPv4:port required");
 const auto colon=c.endpoint.find(':',prefix.size()); require(colon!=std::string::npos,"endpoint port missing");
 const auto host=c.endpoint.substr(prefix.size(),colon-prefix.size()), port=c.endpoint.substr(colon+1);
 std::uint32_t ip=0; std::size_t at=0;
 for(int i=0;i<4;++i) {
  const auto end=host.find('.',at); auto part=host.substr(at,end==std::string::npos?end:end-at);
  require(!part.empty() && part.size()<=3 && (part.size()==1 || part[0]!='0') && part.find_first_not_of("0123456789")==std::string::npos,"noncanonical IPv4");
  const auto n=std::stoul(part); require(n<=255,"IPv4 octet out of range"); ip=(ip<<8)|n;
  require((i==3)==(end==std::string::npos),"IPv4 requires four octets"); at=end+1;
 }
 require(!port.empty() && port.size()<=5 && port[0]!='0' && port.find_first_not_of("0123456789")==std::string::npos,"invalid endpoint port");
 const auto n=std::stoul(port); require(n>0 && n<=65535,"endpoint port out of range");
 const auto first=ip>>24;
 require(first!=0 && first!=127 && first<224 && (ip&255)!=255 && (ip>>16)!=0xa9fe,"non-unicast/test/loopback endpoint forbidden in production");
 c.ipv4=ip; c.port=static_cast<std::uint16_t>(n);
}
}
CommonWorldActivationConfig parseCommonWorldActivationConfig(std::optional<std::string> bytes,const std::filesystem::path& path,bool validateDisabled) {
 CommonWorldActivationConfig c; c.source=path.string();
 if(!bytes || bytes->empty()) {
  if(validateDisabled) { c.valid=false; c.error="complete OFF configuration required"; }
  return c;
 }
 try {
  require(bytes->size()<=65536,"configuration too large");
  std::vector<std::set<std::string>> keys;
  auto j=nlohmann::json::parse(*bytes,[&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& value) {
   require(depth<=8,"configuration nesting too deep");
   if(event==nlohmann::json::parse_event_t::object_start) keys.emplace_back();
   if(event==nlohmann::json::parse_event_t::key) require(keys.back().insert(value.get<std::string>()).second,"duplicate configuration key");
   if(event==nlohmann::json::parse_event_t::object_end) keys.pop_back();
   return true;
  });
  require(j.is_object(),"configuration object required");
  const std::set<std::string> allowed={"enabled","mode","transport","codec","endpoint","owner_id","world_space_id","source_publisher_id","revision_path","queue_capacity","retry_limit","peer_policy"};
  for(auto it=j.begin();it!=j.end();++it) require(allowed.count(it.key())!=0,"unknown configuration field");
  if(j.contains("enabled")) { require(j["enabled"].is_boolean(),"enabled must be boolean"); c.enabled=j["enabled"].get<bool>(); }
  if(!c.enabled && !validateDisabled) return c;
  const auto mode=j.at("mode").get<std::string>(); require(mode=="production" || mode=="test","explicit execution mode required");
  c.mode=mode=="production"?CommonWorldExecutionMode::Production:CommonWorldExecutionMode::Test;
  c.transport=j.at("transport").get<std::string>(); c.codec=j.at("codec").get<std::string>(); c.endpoint=j.at("endpoint").get<std::string>();
  require(c.codec=="monaka-wire-2.2" || (c.mode==CommonWorldExecutionMode::Test && c.codec=="monaka-wire-2.1"),"publisher-aware production requires monaka-wire-2.2; 2.1 is test/legacy only");
  // Local activation never implies remote authority reset or seamless world replacement.
  require(j.at("peer_policy")=="explicit-authority-association-no-implicit-world-replacement","explicit peer authority policy required");
  c.ownerId=j.at("owner_id").get<std::string>(); c.worldSpaceId=j.at("world_space_id").get<std::string>(); c.sourcePublisher=j.at("source_publisher_id").get<std::string>();
  require(id(c.ownerId) && id(c.worldSpaceId) && id(c.sourcePublisher),"invalid required identity");
  c.revisionPath=j.at("revision_path").get<std::string>(); require(!c.revisionPath.empty(),"revision path required");
  if(c.revisionPath.is_relative()) { require(!path.empty(),"relative revision path requires explicit config source"); c.revisionPath=path.parent_path()/c.revisionPath; }
  c.revisionPath=std::filesystem::absolute(c.revisionPath).lexically_normal();
  for(const auto& name:{"queue_capacity","retry_limit"}) if(j.contains(name)) {
   require(j[name].is_number_unsigned(),"positive integer queue/retry bound required"); const auto v=j[name].get<std::uint64_t>();
   require(v<=(name==std::string("queue_capacity")?4096u:32u) && v>=(name==std::string("queue_capacity")?2u:1u),"queue/retry bound out of range");
   if(name==std::string("queue_capacity")) c.queueCapacity=static_cast<std::size_t>(v); else c.retryLimit=static_cast<std::size_t>(v);
  }
  endpoint(c);
 } catch(const std::exception& e) { c.enabled=false; c.valid=false; c.error=dynamic_cast<const nlohmann::json::exception*>(&e)?"configuration JSON validation failed":e.what(); }
 return c;
}
CommonWorldActivationConfig loadCommonWorldActivationConfig(const std::optional<std::filesystem::path>& path) {
 if(!path) return parseCommonWorldActivationConfig({});
 std::ifstream input(*path,std::ios::binary);
 if(!input) throw std::runtime_error("activation config load failed: "+path->string());
 std::string bytes; char b[4096];
 while(input.read(b,sizeof(b)) || input.gcount()) { bytes.append(b,static_cast<std::size_t>(input.gcount())); if(bytes.size()>65536) break; }
 if(input.bad()) throw std::runtime_error("activation config read failed");
 return parseCommonWorldActivationConfig(bytes,*path);
}
}

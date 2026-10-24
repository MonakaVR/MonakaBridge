#include "presentation.hpp"
#include "driver_log.hpp"
#include "render_manifest.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>

namespace mb::steamvr {
namespace {
using J=nlohmann::json;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path,std::size_t maximum){
 std::ifstream f(path,std::ios::binary);require(bool(f),"presentation file unavailable");
 std::string bytes;char block[8192];
 while(f){f.read(block,sizeof(block));bytes.append(block,std::size_t(f.gcount()));require(bytes.size()<=maximum,"presentation file size");}
 require(f.eof(),"presentation read failure");return bytes;
}
J json(const std::string& raw){
 // Reject duplicate object members, including fields inside the config snapshot.
 std::vector<std::set<std::string>> stack;
 auto callback=[&](int,J::parse_event_t event,J& parsed){
  if(event==J::parse_event_t::object_start)stack.emplace_back();
  if(event==J::parse_event_t::key)require(stack.back().insert(parsed.get<std::string>()).second,"duplicate JSON key");
  if(event==J::parse_event_t::object_end)stack.pop_back();return true;
 };
 return J::parse(raw,callback);
}
void keys(const J& value,std::initializer_list<const char*> names){
 require(value.is_object()&&value.size()==names.size(),"presentation field set");
 for(auto name:names)require(value.contains(name),"presentation field missing");
}
std::string hash(const std::string& raw){
 BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE handle{};DWORD size{},written{};
 require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"SHA algorithm");
 struct Close {BCRYPT_ALG_HANDLE a;BCRYPT_HASH_HANDLE* h;~Close(){if(*h)BCryptDestroyHash(*h);BCryptCloseAlgorithmProvider(a,0);}}close{algorithm,&handle};
 require(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&written,0)==0,"SHA size");
 std::vector<UCHAR> object(size);UCHAR digest[32]{};
 require(BCryptCreateHash(algorithm,&handle,object.data(),size,nullptr,0,0)==0,"SHA create");
 require(BCryptHashData(handle,reinterpret_cast<PUCHAR>(const_cast<char*>(raw.data())),ULONG(raw.size()),0)==0,"SHA input");
 require(BCryptFinishHash(handle,digest,32,0)==0,"SHA finish");
 const char* hex="0123456789abcdef";std::string result;
 for(auto value:digest){result+=hex[value>>4];result+=hex[value&15];}return result;
}
bool uuid(const std::string& id){
 if(id.size()!=36)return false;
 for(std::size_t i=0;i<id.size();++i){if(i==8||i==13||i==18||i==23){if(id[i]!='-')return false;}else if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return false;}
 return true;
}
void assets(const std::filesystem::path& package){
 const auto directory=package/L"resources"/L"rendermodels"/L"pico_motion_tracker";
 auto bytes=read(directory/L"assets-manifest.json",8192);
 require(hash(bytes)==MonakaRenderManifestSha,"asset manifest changed");
 auto manifest=json(bytes);keys(manifest,{"model","property","files","source_step_sha256"});
 require(manifest.at("model")=="pico_motion_tracker"&&manifest.at("property")==PicoRenderModel,"model identity");
 keys(manifest.at("files"),{"pico_motion_tracker.obj","pico_motion_tracker.mtl","pico_motion_tracker.png"});
 for(const char* name:{"pico_motion_tracker.obj","pico_motion_tracker.mtl","pico_motion_tracker.png"}){
  const auto path=directory/name;
  require(std::filesystem::weakly_canonical(path).parent_path()==std::filesystem::weakly_canonical(directory),"asset containment");
  require(hash(read(path,8*1024*1024))==manifest.at("files").at(name).get<std::string>(),"asset hash changed");
 }
 auto driver=json(read(package/L"driver.vrdrivermanifest",8192));
 require(driver.at("name")=="monaka_bridge"&&driver.at("resourceOnly")==false,"driver manifest identity");
}
}
std::filesystem::path currentDriverPackage(){
 HMODULE module{};wchar_t path[32768]{};
 require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
       reinterpret_cast<LPCWSTR>(&currentDriverPackage),&module)!=FALSE,"driver module path");
 const auto size=GetModuleFileNameW(module,path,32768);require(size>0&&size<32768,"driver path size");
 auto binary=std::filesystem::path(path);
 require(binary.filename()==L"driver_monaka_bridge.dll"&&binary.parent_path().filename()==L"win64"&&binary.parent_path().parent_path().filename()==L"bin","driver package layout");
 return binary.parent_path().parent_path().parent_path();
}
Presentation Presentation::load(const std::filesystem::path& package){
 try {
  Presentation result;
  const auto directory=package/L"resources"/L"presentation";
  const auto path=directory/L"selection.json";
  require(std::filesystem::weakly_canonical(path).parent_path()==std::filesystem::weakly_canonical(directory),"selection containment");
  auto selection=json(read(path,128*1024));
  require(selection.at("namespace")=="monaka.bridge.presentation"&&selection.at("version").is_number_integer()&&selection.at("version")==1,"presentation namespace/version");
  const auto& assignments=selection.at("assignments");require(assignments.is_array()&&assignments.size()<=256,"presentation assignment count");
  if(assignments.empty()){keys(selection,{"namespace","version","assignments"});return result;}
  keys(selection,{"namespace","version","assignments","config_file","config_sha256","publisher_session"});
  require(selection.at("config_file")=="bridge-config.json","fixed config snapshot name required");
  const auto configPath=directory/L"bridge-config.json";
  require(std::filesystem::weakly_canonical(configPath).parent_path()==std::filesystem::weakly_canonical(directory),"config containment");
  const auto bytes=read(configPath,128*1024);const auto configJson=json(bytes);
  require(hash(bytes)==selection.at("config_sha256").get<std::string>(),"config snapshot hash");
  const auto config=parseConfigDocument(bytes);
  require(configJson.at("mappings").size()==config.bindings.size(),"duplicate physical config mapping");
  require(config.policy==Policy::Steamvr||config.policy==Policy::Both,"snapshot has no Direct route");
  result.publisher_=config.bridgeId;result.revision_=config.revision;
  result.session_=selection.at("publisher_session");require(uuid(result.session_),"publisher session");
  assets(package);
  for(const auto& row:assignments){
   keys(row,{"model_id","device_kind","source_id","device_id","tracker_id","runtime_serial","input_session"});
   require(row.at("model_id")=="pico_motion_tracker"&&row.at("device_kind")=="pico_motion_tracker","explicit PICO model assignment required");
   Assignment entry{row.at("source_id"),row.at("device_id"),row.at("tracker_id"),row.at("input_session")};
   require(uuid(entry.inputSession),"input session");
   const auto it=config.bindings.find({entry.source,entry.device});require(it!=config.bindings.end(),"physical mapping missing");
   const auto& binding=it->second;const auto profile=config.profiles.find(binding.profile);
   require(binding.tracker==entry.tracker&&binding.spaceApproved&&profile!=config.profiles.end()&&profile->second.approved&&!profile->second.evidence.empty()&&config.contracts.count(it->first),"unapproved/unprovisioned mapping");
   auto serial=runtimeSerial(outputSource(config.bridgeId,entry.source),entry.tracker);
   require(row.at("runtime_serial")==serial,"exact runtime serial");
   require(result.assignments_.emplace(serial,std::move(entry)).second,"duplicate model assignment");
  }
  return result;
 } catch(const std::exception& e){DriverLog("monaka_bridge: presentation disabled: %s",e.what());return {};}
}
PresentationState Presentation::evaluate(const std::string& serial,const DirectSample* sample,std::int64_t now)const{
 auto it=assignments_.find(serial);if(it==assignments_.end())return PresentationState::Unassigned;
 if(!sample||!sample->pose)return PresentationState::Hidden;
 const auto& p=*sample->pose;const auto& a=it->second;
 const bool same=p.publisher_id==publisher_&&p.session_id==session_&&p.clock_id==session_&&p.mapping_revision==revision_&&
  p.source_id==a.source&&p.tracker_id==a.tracker&&p.input.source_id==a.source&&p.input.device_id==a.device&&p.input.session_id==a.inputSession;
 if(!same)return PresentationState::Revoked;
 return !sample->absent&&sample->fixedTime>=0&&now>=sample->fixedTime&&now-sample->fixedTime<500000000&&
  p.validity.position&&p.validity.orientation&&p.modality=="full"&&p.tracking_state=="tracked"
  ?PresentationState::Visible:PresentationState::Hidden;
}
}

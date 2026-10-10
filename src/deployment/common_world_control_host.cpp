#include "monaka_bridge/common_world_deployment_readiness.hpp"
#include "monaka_bridge/network.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace monaka_bridge {
using J=nlohmann::json; namespace w=authority_wire;
int runCommonWorldControlHost(const std::filesystem::path& path,const std::filesystem::path& emergencyStop) {
 CommonWorldProductionActivation runtime; CommonWorldActivationConfig config;
 const auto snapshot=[&](const CommonWorldActivationSnapshot& s) { return J::parse(commonWorldRuntimeDiagnostics(s,config,commonWorldBuildCommit())); };
 const auto emit=[&](J result) { std::cout<<result.dump()<<std::endl; };
 try {
  std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error("configuration unavailable");
  const std::string frozen((std::istreambuf_iterator<char>(f)),{});
  config=parseCommonWorldActivationConfig(frozen,path,true);
  if(f.bad() || !config.valid || config.enabled || config.mode!=CommonWorldExecutionMode::Production)
   throw std::runtime_error("complete production configuration OFF required");
  const auto stop=std::filesystem::absolute(emergencyStop);
  if(!std::filesystem::is_directory(stop.parent_path())) throw std::runtime_error("explicit emergency directory unavailable");
#ifdef _WIN32
  const auto input=GetStdHandle(STD_INPUT_HANDLE);
  // Inherited local anonymous/named pipe only, no remote listener/socket or console.
  // Caller owns trust/protection of both producer and command pipe.
  if(GetFileType(input)!=FILE_TYPE_PIPE) throw std::runtime_error("local inherited stdin pipe required");
  emit({{"schema","monaka-common-world-control-2"},{"event","created"},{"snapshot",snapshot(runtime.snapshot())}});
  std::string pending;
  while(!runtime.snapshot().shutdown) {
   // No-data does not end/restart the source. Sentinel remains observable without a
   // blocking source getline. Poll interval is not a real-time emergency guarantee.
   std::ifstream current(path,std::ios::binary);
   const std::string bytes((std::istreambuf_iterator<char>(current)),{});
   if(!current || current.bad() || bytes!=frozen || std::filesystem::exists(stop)) {
    const auto before=runtime.snapshot(); emergencyRevokeCommonWorld(runtime,mb::monotonicNs());
    emit({{"schema","monaka-common-world-control-2"},{"operation_id",mb::uuid()},{"operation","emergency-sentinel-or-config-change"},
     {"accepted",true},{"result","emergency-revoked"},{"before",snapshot(before)},{"after",snapshot(runtime.snapshot())}}); break;
   }
   DWORD available=0;
   if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)) {
    if(GetLastError()!=ERROR_BROKEN_PIPE) throw std::runtime_error("control pipe read failure");
    runtime.disconnectSource(mb::monotonicNs()); runtime.shutdown(mb::monotonicNs());
    emit({{"event","control-pipe-eof"},{"result",pending.empty()?"producer-eof":"truncated-command"},{"snapshot",snapshot(runtime.snapshot())}}); break;
   }
   if(!available) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
   char chunk[4096]; DWORD read=0;
   if(!ReadFile(input,chunk,std::min<DWORD>(available,sizeof(chunk)),&read,nullptr) || !read) throw std::runtime_error("control pipe read failure");
   pending.append(chunk,read);
   if(pending.size()>131072) throw std::runtime_error("bounded command buffer exceeded");
   for(auto newline=pending.find('\n');newline!=std::string::npos;newline=pending.find('\n')) {
    const auto line=pending.substr(0,newline); pending.erase(0,newline+1);
    try {
     std::vector<std::set<std::string>> keys;
     auto j=J::parse(line,[&](int depth,J::parse_event_t event,J& value) {
      if(depth>16) throw std::runtime_error("command nesting");
      if(event==J::parse_event_t::object_start) keys.emplace_back();
      if(event==J::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) throw std::runtime_error("duplicate command key");
      if(event==J::parse_event_t::object_end) keys.pop_back(); return true;
     });
     const std::set<std::string> allowed={"operation_id","operation","runtime_instance","control_session","readiness_token","explicit_enable","source_lease","source_generation","envelope"};
     if(!j.is_object()) throw std::runtime_error("command object required");
     for(auto it=j.begin();it!=j.end();++it) if(!allowed.count(it.key())) throw std::runtime_error("unknown command field");
     CommonWorldControlRequest request;
     request.operationId=j.at("operation_id").get<std::string>(); request.operation=j.at("operation").get<std::string>();
     const std::set<std::string> operations={"status","snapshot","prepare","refresh-readiness","activate","deactivate","owner-revoke","emergency-revoke","source-reconnect","source-authorize","source-disconnect","source-eof","input","transport-reconnect","pump","shutdown"};
     if(!operations.count(request.operation)) throw std::runtime_error("unsupported command");
     request.runtimeInstance=j.at("runtime_instance").get<std::string>(); request.controlSession=j.at("control_session").get<std::string>();
     request.readinessToken=j.value("readiness_token",std::string()); request.explicitEnable=j.value("explicit_enable",false);
     request.sourceLease=j.value("source_lease",std::string()); request.sourceGeneration=j.value("source_generation",std::uint64_t(0));
     if(j.contains("envelope")) { const auto wire=j["envelope"].dump(); w::Envelope e; w::Error error;
      if(!w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(wire.data()),wire.size(),e,error)) throw std::runtime_error("source codec failure");
      request.envelope=std::move(e);
     }
     request.config=path; const auto result=runtime.control(request,mb::monotonicNs());
     emit({{"schema","monaka-common-world-control-2"},{"operation_id",result.operationId},{"operation",result.operation},
      {"result",result.result},{"accepted",result.accepted},{"before",snapshot(result.before)},{"after",snapshot(result.after)}});
     if(result.after.shutdown) break;
    } catch(...) {
     // No raw command/config/exception text enters logs (unknown fields may contain secrets).
     emit({{"schema","monaka-common-world-control-2"},{"accepted",false},{"result","invalid-command"},{"snapshot",snapshot(runtime.snapshot())}});
    }
   }
  }
  return 0;
#else
  throw std::runtime_error("Windows local pipe required");
#endif
 } catch(...) {
  emergencyRevokeCommonWorld(runtime,mb::monotonicNs());
  emit({{"schema","monaka-common-world-control-2"},{"accepted",false},{"result","host-failed-closed"},{"snapshot",snapshot(runtime.snapshot())}}); return 1;
 }
}
}

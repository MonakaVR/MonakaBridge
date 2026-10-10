#include "monaka_bridge/trusted_live_source.hpp"
#include "monaka_bridge/common_world_deployment_readiness.hpp"
#include "monaka_bridge/network.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <set>
#include <cctype>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#endif
using namespace monaka_bridge; using J=nlohmann::json;
namespace {
std::string readConfig(const std::filesystem::path& p) {
 std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("config unavailable");
 std::string b((std::istreambuf_iterator<char>(f)),{}); if(f.bad() || b.size()>8192) throw std::runtime_error("config bound"); return b;
}
#ifdef _WIN32
struct Handle { HANDLE h=INVALID_HANDLE_VALUE; ~Handle(){if(h!=INVALID_HANDLE_VALUE) CloseHandle(h);} };
struct Security {
 PSECURITY_DESCRIPTOR descriptor=nullptr;
 SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
 Security() {
  Handle token; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.h)) throw std::runtime_error("token");
  DWORD size=0; GetTokenInformation(token.h,TokenUser,nullptr,0,&size);
  std::vector<unsigned char> data(size);
  if(!size || !GetTokenInformation(token.h,TokenUser,data.data(),size,&size)) throw std::runtime_error("SID");
  LPWSTR sid=nullptr;
  if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid)) throw std::runtime_error("SID");
  const std::wstring acl=L"D:P(A;;GA;;;"+std::wstring(sid)+L")"; LocalFree(sid);
  if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)) throw std::runtime_error("ACL");
  attributes.lpSecurityDescriptor=descriptor;
 }
 ~Security(){if(descriptor) LocalFree(descriptor);}
};
#endif
}
int main(int argc,char** argv) {
 if(argc==2 && std::string(argv[1])=="--build-identity"){std::cout<<commonWorldBuildCommit()<<std::endl;return 0;}
 if(argc==1){std::cout<<J({{"schema","monaka-trusted-live-producer-snapshot-1"},{"build_commit",commonWorldBuildCommit()},{"lifecycle","DisabledByDefault"},{"connected",false}}).dump()<<std::endl;return 0;}
 try {
  if(argc!=3 || std::string(argv[1])!="--run") throw std::runtime_error("explicit --run config required");
  const std::filesystem::path path=argv[2]; const auto frozen=readConfig(path);
  std::set<std::string> configKeys;
  const auto j=J::parse(frozen,[&](int depth,J::parse_event_t event,J& value) {
   if(depth>2) throw std::runtime_error("config depth");
   if(event==J::parse_event_t::key && !configKeys.insert(value.get<std::string>()).second) throw std::runtime_error("duplicate config field");
   return true;
  });
  const std::set<std::string> allowed={"enabled","source_publisher_id","source_id","source_space_id","queue_capacity","pipe_name"};
  for(auto it=j.begin();it!=j.end();++it) if(!allowed.count(it.key())) throw std::runtime_error("config field");
  if(j.at("enabled")!=false) throw std::runtime_error("OFF config required");
  const auto name=j.at("pipe_name").get<std::string>();
  if(name.empty() || name.size()>96 || !std::all_of(name.begin(),name.end(),[](char c){return std::isalnum(static_cast<unsigned char>(c)) || c=='-' || c=='_';})) throw std::runtime_error("explicit local pipe name");
  TrustedLiveSourceConfig config{j.at("source_publisher_id").get<std::string>(),j.at("source_id").get<std::string>(),j.at("source_space_id").get<std::string>(),j.at("queue_capacity").get<std::size_t>()};
  TrustedLiveSourceProducer producer(config,[]{return mb::uuid();}); const auto runtime=mb::uuid();
  const auto emit=[&](const std::string& event,std::optional<authority_wire::Envelope> e=std::nullopt) {
   J out={{"event",event},{"snapshot",J::parse(producer.snapshot(commonWorldBuildCommit(),runtime))}};
   if(e) {std::string bytes;authority_wire::Error error;if(!authority_wire::EncodeEnvelope(*e,bytes,error))throw std::runtime_error("codec");out["envelope"]=J::parse(bytes);}
   std::cout<<out.dump()<<std::endl; if(!std::cout) throw std::runtime_error("output pipe failed");
  };
#ifdef _WIN32
  // One current-user-only, remote-rejecting local message pipe; never an IP listener.
  Security security; Handle pipe; bool client=false;
  const std::wstring pipeName=L"\\\\.\\pipe\\"+std::wstring(name.begin(),name.end());
  const auto reset=[&] {
   if(pipe.h==INVALID_HANDLE_VALUE) {
    pipe.h=CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_INBOUND|FILE_FLAG_FIRST_PIPE_INSTANCE,
     PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,1,8192,8192,0,&security.attributes);
    if(pipe.h==INVALID_HANDLE_VALUE) throw std::runtime_error("local pipe create");
   } else if(!DisconnectNamedPipe(pipe.h) && GetLastError()!=ERROR_PIPE_NOT_CONNECTED) {
    throw std::runtime_error("local pipe disconnect");
   }
   // Retain the secured first-instance server handle across reconnect. Closing and
   // recreating it while the old client still holds a handle is a collision race.
   client=false;
   if(ConnectNamedPipe(pipe.h,nullptr) || GetLastError()==ERROR_PIPE_CONNECTED) client=true;
   else if(GetLastError()!=ERROR_PIPE_LISTENING && GetLastError()!=ERROR_NO_DATA) throw std::runtime_error("local pipe listen");
  };
  reset(); emit("created"); std::string commands;
  const auto input=GetStdHandle(STD_INPUT_HANDLE);
  if(GetFileType(input)!=FILE_TYPE_PIPE) throw std::runtime_error("inherited local control pipe required");
  for(;;) {
   if(readConfig(path)!=frozen) throw std::runtime_error("frozen producer config changed");
   DWORD available=0;
   if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)) {
    if(GetLastError()!=ERROR_BROKEN_PIPE || !commands.empty()) throw std::runtime_error("control framing/failure");
    producer.disconnect("OrderlyShutdown");emit("orderly-shutdown");return 0;
   }
   if(available) {
    char bytes[4096];DWORD got=0;
    if(!ReadFile(input,bytes,std::min<DWORD>(available,sizeof(bytes)),&got,nullptr)) throw std::runtime_error("control pipe");
    commands.append(bytes,got); if(commands.size()>8192) throw std::runtime_error("control bound");
    auto newline=commands.find('\n');
    while(newline!=std::string::npos) {
     const auto command=J::parse(commands.substr(0,newline));commands.erase(0,newline+1);
     const auto op=command.at("operation").get<std::string>();
     if(command.size()!=1) throw std::runtime_error("control field");
     if(op=="shutdown") { producer.disconnect("OrderlyShutdown");emit("orderly-shutdown");return 0; }
     if(op=="restart-publication") {producer.disconnect("Disconnected");reset();emit("disconnect");}
     else if(op=="snapshot") emit("snapshot");
     else throw std::runtime_error("control operation");
     newline=commands.find('\n');
    }
   }
   if(!client) {
    if(ConnectNamedPipe(pipe.h,nullptr) || GetLastError()==ERROR_PIPE_CONNECTED) client=true;
    else if(GetLastError()!=ERROR_PIPE_LISTENING && GetLastError()!=ERROR_NO_DATA) throw std::runtime_error("local pipe connect");
   }
   if(client) {
    char bytes[2048];DWORD got=0;
    if(ReadFile(pipe.h,bytes,sizeof(bytes),&got,nullptr)) {
     if(got) {
      const auto result=producer.receiveExport(std::string(bytes,got),mb::monotonicNs());
      if(result=="source-changed") emit("source-changed");
      while(auto envelope=producer.take()) emit("envelope",*envelope);
      if(!producer.connected()) {
       const auto lifecycle=J::parse(producer.snapshot(commonWorldBuildCommit(),runtime)).at("lifecycle").get<std::string>();
       reset();emit(lifecycle=="OrderlyShutdown"?"orderly-shutdown":lifecycle=="Disconnected"?"disconnect":
        lifecycle=="Backpressure"?"backpressure":lifecycle=="InvalidExport"?"invalid-export":"stream-failure");
      }
     } else producer.noData();
    } else {
     const auto error=GetLastError();
     if(error==ERROR_NO_DATA) producer.noData();
     else if(error==ERROR_BROKEN_PIPE || error==ERROR_PIPE_NOT_CONNECTED) {producer.disconnect("Disconnected");reset();emit("disconnect");}
     else {producer.disconnect("StreamFailure");reset();emit("stream-failure");}
    }
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
#else
  throw std::runtime_error("Windows local pipe required");
#endif
 } catch(...) {
  std::cout<<J({{"event","stream-failure"},{"result","producer-failed-closed"},{"build_commit",commonWorldBuildCommit()}}).dump()<<std::endl;return 1;
 }
}

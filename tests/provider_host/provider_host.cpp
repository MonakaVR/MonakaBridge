// Loads the ordinary product DLL; no product Provider/TrackerDevice source is linked here.
#include "../helpers.hpp" // Parse typed contracts before Windows' legacy STRICT/near macros.
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include "strict_host.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>

namespace pt = provider_test;
namespace fs = std::filesystem;
using pt::require;
using pt::Json;
uint16_t Port=0;
const std::string Serial="monaka-direct:313a7073:74"; // publisher p + source s + tracker t, independently fixed.

struct PortReservation {
    SOCKET socket=INVALID_SOCKET;
    bool started=false;
    explicit PortReservation() {
        WSADATA data{}; require(WSAStartup(MAKEWORD(2,2),&data)==0,"probe WSAStartup"); started=true;
        socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        require(socket!=INVALID_SOCKET,"probe socket");
        BOOL exclusive=TRUE;
        require(setsockopt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&exclusive),sizeof(exclusive))==0,"probe exclusive");
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK); address.sin_port=htons(Port);
        if(bind(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0) {
            const auto error=WSAGetLastError(); close();
            throw std::runtime_error("PORT_BLOCKED:"+std::to_string(error));
        }
        if(Port==0){int size=sizeof(address);require(getsockname(socket,reinterpret_cast<sockaddr*>(&address),&size)==0,"ephemeral port query");Port=ntohs(address.sin_port);require(Port>=49152,"ephemeral port range");}
    }
    void close() { if(socket!=INVALID_SOCKET)closesocket(socket);socket=INVALID_SOCKET;if(started)WSACleanup();started=false; }
    ~PortReservation(){close();}
};
std::string sha256(const fs::path& path) {
    std::ifstream stream(path,std::ios::binary); require(bool(stream),"hash input open");
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"SHA256 algorithm");
    DWORD length=0,bytes=0; require(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&bytes,0)==0,"SHA256 length");
    std::vector<UCHAR> object(length); UCHAR digest[32]{};
    require(BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,0)==0,"SHA256 create");
    char block[65536];while(stream){stream.read(block,sizeof(block));auto size=stream.gcount();if(size)require(BCryptHashData(hash,reinterpret_cast<PUCHAR>(block),ULONG(size),0)==0,"SHA256 data");}
    require(BCryptFinishHash(hash,digest,sizeof(digest),0)==0,"SHA256 finish");
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    std::string result;const char* digits="0123456789abcdef";for(auto b:digest){result+=digits[b>>4];result+=digits[b&15];}return result;
}
struct Module {
    HMODULE library{};vr::IServerTrackedDeviceProvider* provider{};
    using Factory=void*(__cdecl*)(const char*,int*);
    Factory factory{};pt::Events& events;
    Module(const fs::path& path,pt::Events& e):events(e) {
        library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        require(library!=nullptr,"LoadLibraryEx product DLL failed: "+std::to_string(GetLastError()));
        factory=reinterpret_cast<Factory>(GetProcAddress(library,"HmdDriverFactory"));require(factory,"GetProcAddress HmdDriverFactory");
        wchar_t loaded[32768]{};require(GetModuleFileNameW(library,loaded,32768)>0,"loaded module path");
        require(fs::equivalent(path,loaded),"loaded a different product DLL");
        events.write("LoadLibrary",{{"path",fs::path(loaded).u8string()},{"sha256",sha256(loaded)}});
        int error=vr::VRInitError_None;
        provider=static_cast<vr::IServerTrackedDeviceProvider*>(factory(vr::IServerTrackedDeviceProvider_Version,&error));
        events.write("factory",{{"interface",vr::IServerTrackedDeviceProvider_Version},{"pointer",reinterpret_cast<uintptr_t>(provider)}});
        require(provider,"factory returned null");
    }
    void unload() {
        require(library && FreeLibrary(library)!=FALSE,"FreeLibrary product DLL failed");
        library=nullptr;provider=nullptr;factory=nullptr;
    }
    ~Module(){if(library)FreeLibrary(library);}
};
struct Session {
    Module& module;pt::StrictHost host;bool attempted=false,finished=false,verifyPort=true;vr::EVRInitError error{};
    Session(Module& m,std::string missing={},bool portEvidence=true):module(m),host(m.events),verifyPort(portEvidence) {
        host.inputPort=Port;host.missingInterface=std::move(missing);attempted=true;
        module.events.write("Init",{{"phase","call"}});error=module.provider->Init(&host);
        module.events.write("Init",{{"phase","return"},{"result",int(error)}});
    }
    void frame(){require(error==vr::VRInitError_None,"RunFrame after failed Init");module.provider->RunFrame();host.activatePending();}
    void pump(int milliseconds) {
        const auto end=mb::monotonicNs()+int64_t(milliseconds)*1000000;
        do {frame();std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(mb::monotonicNs()<end);
    }
    void until(const std::function<bool()>& predicate,int timeout=800) {
        const auto deadline=mb::monotonicNs()+int64_t(timeout)*1000000;
        while(!predicate()){require(mb::monotonicNs()<deadline,"host observation timeout");pump(2);}
    }
    const vr::DriverPose_t& pose(uint32_t id=1)const{return host.devices.at(id).pose;}
    void finish() {
        if(finished)return;
        host.deactivateAll();
        const auto count=host.totalPoses;
        if(error==vr::VRInitError_None && !host.devices.empty())module.provider->RunFrame();
        require(host.totalPoses==count,"update after Deactivate");
        module.events.write("Cleanup",{{"phase","call"}});module.provider->Cleanup();finished=true;
        module.events.write("Cleanup",{{"phase","return"}});
        host.devices.clear();host.alive=false;
        const auto calls=host.calls;module.provider->RunFrame();require(host.calls==calls,"callback after Cleanup");
        if(verifyPort){PortReservation released;}
        module.events.write("CleanupEvidence",{{"port",Port},{"exclusive_rebind",verifyPort?Json(true):Json("NOT_RUN")},{"retired_context_callbacks",host.calls-calls},{"retained_device_pointers",host.devices.size()}});
    }
    ~Session(){if(attempted&&!finished)try{finish();}catch(const std::exception& e){std::cerr<<"cleanup failure: "<<e.what()<<'\n';}}
};
template<class T>T fixture(const std::string& name) {
    std::ifstream stream(fs::path(ROOT_DIR)/"third_party/monaka-protocol-v22/fixtures/v2"/name);
    std::string bytes((std::istreambuf_iterator<char>(stream)),{});mb::c1::Envelope envelope;mb::c1::Error error;
    require(mb::c1::DecodeEnvelope(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),envelope,error),"canonical fixture Decode: "+error.message);
    return std::get<T>(envelope);
}
mb::c1::MtpPose full() {
    auto p=fixture<mb::c1::MtpPose>("mtp-pose.json");
    p.publisher_id="p";p.source_id=p.input.source_id="s";p.tracker_id="t";p.sequence=10;p.mapping_revision=1;
    p.timestamp_ns=1000000000000;p.sent_at_ns=p.timestamp_ns; p.session_id=p.clock_id=S1;
    p.orientation=mb::Quat{.5,-.5,.5,.5};
    p.linear_velocity=mb::Vec{4,5,6};p.angular_velocity=mb::Vec{.1,.2,.3};p.linear_acceleration=mb::Vec{7,8,9};
    p.capabilities={"position","orientation","linear_velocity","angular_velocity","linear_acceleration"};return p;
}
void send(mb::Udp& sender,pt::Events& events,const mb::c1::Envelope& envelope) {
    const auto bytes=wire(envelope);require(sender.send(Port,bytes),"UDP send");
    events.write("MtpSend",{{"payload",Json::parse(bytes)},{"port",Port},{"source_port",sender.port()},{"bytes",bytes.size()}});
}
bool close(double a,double b){return std::abs(a-b)<=1e-7;}
void rotation(const vr::HmdQuaternion_t& actual,const mb::Quat& xyzw) {
    const double dot=actual.x*xyzw[0]+actual.y*xyzw[1]+actual.z*xyzw[2]+actual.w*xyzw[3];
    const double norm=actual.x*actual.x+actual.y*actual.y+actual.z*actual.z+actual.w*actual.w;
    require(close(norm,1),"quaternion unit norm");
    require(2*std::acos(std::min(1.0,std::abs(dot)))<1e-6,"quaternion angular distance");
}
void validFull(const vr::DriverPose_t& actual,const mb::c1::MtpPose& p) {
    require(actual.deviceIsConnected && actual.poseIsValid && actual.result==vr::TrackingResult_Running_OK,"FULL connection/valid/result");
    for(int i=0;i<3;++i){require(close(actual.vecPosition[i],p.position->at(i)),"position metres");
        if(p.linear_velocity)require(close(actual.vecVelocity[i],p.linear_velocity->at(i)),"linear velocity");
        if(p.angular_velocity)require(close(actual.vecAngularVelocity[i],p.angular_velocity->at(i)),"angular velocity");
        if(p.linear_acceleration)require(close(actual.vecAcceleration[i],p.linear_acceleration->at(i)),"linear acceleration");}
    rotation(actual.qRotation,*p.orientation);
    require(actual.poseTimeOffset>=-.100 && actual.poseTimeOffset<=.020,"poseTimeOffset clamp");
    require(close(actual.qWorldFromDriverRotation.w,1)&&close(actual.qDriverFromHeadRotation.w,1),"identity driver transforms");
    for(int i=0;i<3;++i)require(close(actual.vecWorldFromDriverTranslation[i],0)&&close(actual.vecDriverFromHeadTranslation[i],0),"no second world/mount translation");
}
void directSuite(Module& module,Json& cases) {
    auto& events=module.events;mb::Udp sender;
    events.scenario="D1-01";
    for(const char* name: {"IServerTrackedDeviceProvider_999","unknown",static_cast<const char*>(nullptr)}) {
        int error=0;require(module.factory(name,&error)==nullptr && error==vr::VRInitError_Init_InterfaceNotFound,"factory interface rejection");
        events.write("factory",{{"interface",name?Json(name):Json(nullptr)},{"result",error},{"rejected",true}});
    }
    require(module.factory("wrong",nullptr)==nullptr,"factory null error pointer");cases["D1-01"]="PASS";
    events.scenario="D1-02";
    {
        Session run(module);require(run.error==vr::VRInitError_None,"Init");
        bool secondOwnerRejected=false;
        try {PortReservation secondOwner;}
        catch(const std::exception& e) {
            const std::string error=e.what();
            secondOwnerRejected=error=="PORT_BLOCKED:10048"||error=="PORT_BLOCKED:10013";
            if(!secondOwnerRejected)throw;
            events.write("SecondOwnerRejected",{{"detail",error},{"port",Port}});
        }
        require(secondOwnerRejected,"product receiver did not exclusively own 29812");
        require(run.host.interfaceRequests.size()==6,"six Init interfaces");run.pump(5);
        require(run.host.devices.empty() && run.host.totalPoses==0,"no registration before input");run.finish();
    }
    cases["D1-02"]="PASS";
    events.scenario="D1-03/04/05/06/07/10";
    {
        Session run(module);require(run.error==vr::VRInitError_None,"second Init");
        auto p=full();send(sender,events,p);
        run.until([&]{return run.host.totalPoses>0;});
        require(run.host.devices.size()==1 && run.host.devices.at(1).serial==Serial && run.host.attempts.at(Serial)==1,"single GenericTracker serial");
        validFull(run.pose(),p);cases["D1-03"]="PASS";cases["D1-04"]="PASS";
        // Sign-equivalent quaternion is checked by angular distance, not component equality.
        p.sequence++;for(auto& x:*p.orientation)x=-x;send(sender,events,p);run.pump(10);validFull(run.pose(),p);
        auto partial=p;partial.sequence++;partial.modality="rotation_only";partial.validity={false,true};partial.position.reset();partial.tracking_state="degraded";
        partial.linear_velocity.reset();partial.linear_acceleration.reset();partial.confidence.position=0;
        send(sender,events,partial);run.pump(10);
        require(run.pose().deviceIsConnected && !run.pose().poseIsValid && run.pose().result==vr::TrackingResult_Fallback_RotationOnly,"ROTATION_ONLY must not be 6DoF valid");
        rotation(run.pose().qRotation,*partial.orientation);
        for(double v:run.pose().vecPosition)require(v==0,"rotation-only stale position");
        auto none=partial;none.sequence++;none.modality="none";none.validity={false,false};none.orientation.reset();none.angular_velocity.reset();none.tracking_state="lost";none.confidence.orientation=0;
        send(sender,events,none);run.pump(10);
        require(run.pose().deviceIsConnected && !run.pose().poseIsValid && run.pose().result==vr::TrackingResult_Running_OutOfRange,"NONE connected but invalid");
        for(double v:run.pose().vecPosition)require(v==0,"NONE stale position");cases["D1-05"]="PASS";
        p.sequence=20;p.timestamp_ns+=100000000; p.sent_at_ns=p.timestamp_ns;send(sender,events,p);run.pump(10);validFull(run.pose(),p);
        const auto acceptedAt=mb::monotonicNs();run.pump(200);
        auto duplicate=p;duplicate.position=mb::Vec{900,900,900};send(sender,events,duplicate);
        duplicate.sequence=19;send(sender,events,duplicate);run.pump(10);validFull(run.pose(),p);
        // Bad data is injected only as test stimulus; production decoding remains the fixed codec.
        for(const auto token:{"NaN","Infinity","1e999"}) {
            auto bad=Json::parse(wire(p));bad["sequence"]="999";bad["position"][0]="NONFINITE";
            auto bytes=bad.dump();const auto offset=bytes.find("\"NONFINITE\"");bytes.replace(offset,11,token);
            require(sender.send(Port,bytes),"malformed UDP send");events.write("MalformedSend",{{"token",token},{"payload",bytes}});
        }
        require(sender.send(Port,"not JSON"),"bad UDP send");run.pump(10);validFull(run.pose(),p);
        auto invalid=p;invalid.position->at(0)=std::numeric_limits<double>::quiet_NaN();std::string encoded;mb::c1::Error error;
        require(!mb::c1::EncodeEnvelope(invalid,encoded,error),"canonical codec accepts NaN");
        invalid.position->at(0)=std::numeric_limits<double>::infinity();require(!mb::c1::EncodeEnvelope(invalid,encoded,error),"canonical codec accepts Inf");
        while(mb::monotonicNs()-acceptedAt<440000000)run.pump(2);
        require(run.pose().poseIsValid,"lease expired too early");
        run.until([&]{return !run.pose().deviceIsConnected;},400);
        const auto expiredAfter=mb::monotonicNs()-acceptedAt;
        require(expiredAfter>=440000000 && expiredAfter<=800000000,"500 ms lease observation tolerance");
        require(!run.pose().poseIsValid && run.pose().result==vr::TrackingResult_Uninitialized,"expired invalid state");
        events.write("LeaseExpiry",{{"policy_ms",500},{"observed_after_ns",expiredAfter},{"bounds_ns",{440000000,800000000}}});cases["D1-06"]="PASS";
        // A quiet old session may be replaced; delayed old-session packets stay retired.
        auto restart=p;restart.session_id=restart.clock_id=S2;restart.sequence=0;restart.position=mb::Vec{8,7,6};
        restart.timestamp_ns+=100000000;restart.sent_at_ns=restart.timestamp_ns;send(sender,events,restart);run.pump(10);validFull(run.pose(),restart);
        p.sequence=100; p.position=mb::Vec{777,777,777};send(sender,events,p);run.pump(10);validFull(run.pose(),restart);
        require(run.host.devices.size()==1 && run.host.attempts.at(Serial)==1,"session replacement object growth");
        auto second=restart;second.tracker_id="u";second.sequence=1;send(sender,events,second);run.until([&]{return run.host.devices.size()==2 && run.host.devices.at(2).poses>0;});
        require(run.host.devices.at(2).serial=="monaka-direct:313a7073:75","second logical serial collision");cases["D1-06"]={{"duplicate_reorder_lease","PASS"},{"session_retirement_no_duplicate","PASS"}};
        auto state=fixture<mb::c1::MtpTrackerState>("tracker-state.json");
        state.publisher_id="p";state.source_id="s";state.tracker_id="t";state.session_id=state.clock_id=S2;state.sequence=0;state.mapping_revision=1;
        state.timestamp_ns=restart.timestamp_ns;state.sent_at_ns=state.timestamp_ns;state.battery=mb::c1::Battery{.75,true,state.timestamp_ns};
        state.capabilities.push_back("battery_fraction");state.capabilities.push_back("charging");send(sender,events,state);run.pump(10);
        const auto& properties=run.host.devices.at(1).properties;
        require(properties.at(vr::Prop_DeviceProvidesBatteryStatus_Bool)==true && close(properties.at(vr::Prop_DeviceBatteryPercentage_Float).get<double>(),.75) && properties.at(vr::Prop_DeviceIsCharging_Bool)==true,"battery properties");
        require(!properties.count(vr::Prop_RenderModelName_String),"ModelNumber is not a RenderModel");cases["D1-10"]="PASS";
        run.finish();
    }
    events.scenario="D1-07";
    {
        Session run(module);require(run.error==vr::VRInitError_None,"refusal Init");run.host.rejectNext=1;
        send(sender,events,full());
        const auto deadline=mb::monotonicNs()+500000000;
        while(run.host.attempts.empty()){require(mb::monotonicNs()<deadline,"refusal timeout");run.frame();}
        require(run.host.devices.empty() && run.host.totalPoses==0,"rejected pointer retained/activated");
        run.until([&]{return run.host.totalPoses>0;});require(run.host.devices.size()==1 && run.host.attempts.at(Serial)==2,"refusal retry ownership");
        validFull(run.pose(),full());run.finish();cases["D1-07"]="PASS";
    }
    events.scenario="D1-09";
    for(const auto name:{vr::IVRServerDriverHost_Version,vr::IVRSettings_Version,vr::IVRProperties_Version,vr::IVRDriverLog_Version,vr::IVRDriverManager_Version,vr::IVRResources_Version}) {
        Session run(module,name);require(run.error==vr::VRInitError_Init_InterfaceNotFound,"missing Init interface accepted");
        require(run.host.devices.empty() && run.host.totalPoses==0,"failed Init produced devices");run.finish();
    }
    {
        auto blocker=std::make_unique<PortReservation>();Session run(module);
        require(run.error!=vr::VRInitError_None,"occupied port Init succeeded");blocker.reset();run.finish();
    }
    {
        Session run(module);require(run.error==vr::VRInitError_None,"re-init after failures");run.pump(5);require(run.host.devices.empty(),"old feed after re-init");run.finish();
    }
    cases["D1-08"]="PASS";
    cases["D1-09"]={{"initialization_missing_interfaces","PASS"},{"owned_port_bind_failure","PASS"},{"cleanup_reinit","PASS"},{"OS_recvfrom_syscall_failure","NOT RUN"}};
}

void fakeContract(pt::Events& events) {
    struct Dummy final:vr::ITrackedDeviceServerDriver {
        vr::EVRInitError Activate(uint32_t) override{return vr::VRInitError_None;}
        void Deactivate()override{} void EnterStandby()override{}
        void* GetComponent(const char*)override{return nullptr;}
        void DebugRequest(const char*,char*,uint32_t)override{}
        vr::DriverPose_t GetPose()override{return {};}
    } dummy;
    pt::StrictHost strict(events);events.scenario="strict-fake-negative-controls";unsigned rejected=0;
    for(const auto& call:std::vector<std::function<void()>>{
        [&]{strict.GetGenericInterface("unknown",nullptr);},
        [&]{strict.TrackedDeviceToPropertyContainer(999);},
        [&]{strict.TrackedDevicePoseUpdated(999,vr::DriverPose_t{},sizeof(vr::DriverPose_t));},
        [&]{strict.WritePropertyBatch(0,nullptr,0);}}) {
        try{call();}catch(const std::runtime_error&){++rejected;}
    }
    require(strict.TrackedDeviceAdded("mock-selftest",vr::TrackedDeviceClass_GenericTracker,&dummy),"fake control registration");
    for(const auto& call:std::vector<std::function<void()>>{
        [&]{strict.TrackedDeviceAdded("mock-selftest",vr::TrackedDeviceClass_GenericTracker,&dummy);},
        [&]{strict.TrackedDevicePoseUpdated(1,vr::DriverPose_t{},sizeof(vr::DriverPose_t));},
        [&]{strict.TrackedDeviceToPropertyContainer(1);}}) {
        try{call();}catch(const std::runtime_error&){++rejected;}
    }
    strict.devices.at(1).stage=pt::StrictHost::Stage::Active;
    char value[]="mock-selftest";vr::PropertyWrite_t property{};
    property.prop=vr::Prop_SerialNumber_String;property.writeType=vr::PropertyWrite_Set;
    property.pvBuffer=value;property.unBufferSize=sizeof(value);property.unTag=vr::k_unBoolPropertyTag;
    try{strict.WritePropertyBatch(0x10001,&property,1);}catch(const std::runtime_error&){++rejected;}
    require(rejected==8,"strict fake negative controls");events.write("StrictFakeControls",{{"rejected",rejected}});
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX|SEM_NOGPFAULTERRORBOX);
    const bool factoryOnly=argc==4&&std::string(argv[1])=="--factory-only";
    const bool loadFailure=argc==4&&std::string(argv[1])=="--load-failure";
    const bool missingOnly=argc==4&&std::string(argv[1])=="--missing-interfaces-only";
    if(argc!=3&&!factoryOnly&&!loadFailure&&!missingOnly)return 2;
    const fs::path dll=fs::absolute(argv[factoryOnly||loadFailure||missingOnly?2:1]);
    const fs::path out=fs::absolute(argv[factoryOnly||loadFailure||missingOnly?3:2]);
    require(!fs::exists(out),"new run directory required");fs::create_directories(out);
    pt::Events events((out/"host-events.jsonl").string());
    Json result={{"result","FAIL"},{"real_steamvr_hil","NOT RUN"},{"model_render_hil","NOT RUN"},{"physical_cutover","PHYSICAL_CUTOVER_PENDING"}};
    int exit=1;
    for(int i=1;i<=10;++i)result["cases"][std::string("D1-")+(i<10?"0":"")+std::to_string(i)]="NOT RUN";
    result["reproduction_argv"]=std::vector<std::string>(argv,argv+argc);
    try {
        if(std::string(argv[1])=="--fake-contract") {
            fakeContract(events);result["result"]="PASS";result["scope"]="fake negative controls only; no product DLL/provider executed";
            std::ofstream(out/"result.json")<<result.dump(2);std::cout<<result.dump()<<'\n';return 0;
        }
        if(loadFailure) {
            require(!fs::exists(dll),"load-failure fixture path must not exist");
            SetLastError(0);auto handle=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            require(!handle,"missing DLL unexpectedly loaded");const auto code=GetLastError();require(code==ERROR_MOD_NOT_FOUND,"unexpected missing DLL error");
            events.write("LoadLibraryFailure",{{"winerror",code},{"expected",true}});
            result["result"]="PASS";result["scope"]="D1-09 missing DLL subcase only";result["missing_dll_winerror"]=code;
            std::ofstream(out/"cleanup-evidence.txt")<<"DLL handle not created; no Init, sockets or process control\n";
            std::ofstream(out/"result.json")<<result.dump(2);std::cout<<result.dump()<<'\n';return 0;
        }
        result["driver_dll"]=dll.u8string();result["driver_sha256"]=sha256(dll);
        if(!factoryOnly&&!missingOnly) {
            try {PortReservation preflight;events.write("PortPreflight",{{"port",Port},{"exclusive_bind",true}});}
            catch(const std::exception& e){
                const std::string message=e.what();if(message.rfind("PORT_BLOCKED:",0)!=0)throw;
                result["result"]="BLOCKED";result["reason"]=message=="PORT_BLOCKED:10048"?"BLOCKED_PORT_IN_USE":"BLOCKED_PORT_UNAVAILABLE";result["detail"]=message;
                events.write("PortPreflight",{{"port",Port},{"exclusive_bind",false},{"detail",message},{"reason",result["reason"]}});
                std::ofstream(out/"cleanup-evidence.txt")<<"BLOCKED before LoadLibrary/Provider Init\nprobe socket closed; WSAStartup balanced\nno C1/MTP input sent\nno child process created\nno service/driver registration/configuration changed\nexisting port owner left untouched\n";
                std::ofstream(out/"result.json")<<result.dump(2);std::cerr<<result.dump()<<'\n';return 77;
            }
        }
        require(!GetModuleHandleW(L"vrclient_x64.dll"),"test process unexpectedly has a SteamVR client loaded");
        {
            Module module(dll,events);events.scenario="D1-01";
            if(missingOnly) {
                // Pinned header InitServer checks these six interfaces before
                // the product reaches its UDP constructor. No port operation.
                events.scenario="D1-09-missing-interface";
                for(const auto name:{vr::IVRServerDriverHost_Version,vr::IVRSettings_Version,vr::IVRProperties_Version,vr::IVRDriverLog_Version,vr::IVRDriverManager_Version,vr::IVRResources_Version}) {
                    Session run(module,name,false);require(run.error==vr::VRInitError_Init_InterfaceNotFound,"missing required interface not rejected");
                    require(run.host.devices.empty()&&run.host.totalPoses==0,"failed Init emitted devices/poses");run.finish();
                }
                result["scope"]="D1-09 six required interface refusals only; no socket or pose input";
                result["missing_interface_cases"]=6;
            } else if(factoryOnly) {
                for(const char* name:{"IServerTrackedDeviceProvider_999","unknown",static_cast<const char*>(nullptr)}) {
                    int error=0;require(module.factory(name,&error)==nullptr&&error==vr::VRInitError_Init_InterfaceNotFound,"factory interface rejection");
                    events.write("factory",{{"interface",name?Json(name):Json(nullptr)},{"result",error},{"rejected",true}});
                }
                require(module.factory("wrong",nullptr)==nullptr,"factory null error pointer");result["cases"]["D1-01"]="PASS";
                result["scope"]="product DLL load/factory/unload only; no Init/UDP/input";
            } else directSuite(module,result["cases"]);
            module.unload();
        }
        events.write("FreeLibrary",{{"returned",true}});
        if(!factoryOnly&&!missingOnly){PortReservation released;}
        require(!GetModuleHandleW(L"vrclient_x64.dll"),"SteamVR client was loaded");
        std::ofstream(out/"cleanup-evidence.txt")<<(factoryOnly?"factory-only: no Init/socket/stimulus; product DLL FreeLibrary completed; no test child remaining\n":missingOnly?"six failed Init contexts retired; Cleanup and FreeLibrary completed; no UDP acquisition or stimulus\n":"exclusive UDP rebind PASS; Host Deactivate and Provider Cleanup completed; no retained pointers/context callbacks\n");
        result["result"]="PASS";result["gate"]=factoryOnly?"FACTORY_ONLY_PASS":missingOnly?"MISSING_INTERFACE_SUBCASES_PASS":"PRODUCTION_PROVIDER_MOCK_HOST_PASS";exit=0;
    } catch(const std::exception& e) {
        result["error"]=e.what();result["failed_phase"]=events.scenario;events.write("Failure",{{"message",e.what()}});std::cerr<<e.what()<<'\n';
    }
    std::ofstream(out/"result.json")<<result.dump(2);std::cout<<result.dump()<<'\n';return exit;
}

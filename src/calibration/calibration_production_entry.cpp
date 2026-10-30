#include "monaka_bridge/calibration_runtime_provider.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include "monaka_bridge/output_approval_authority.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>

namespace mb::local_calibration {
namespace {
void need(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
class OpenVrReference final:public CalibrationReferenceApi {
 vr::IVRSystem* system_=nullptr;
 void ready(){need(system_!=nullptr,"REFERENCE_NOT_INITIALIZED");}
public:
 void initialize(vr::EVRApplicationType type)override{
  need(!system_&&type==vr::VRApplication_Background,"REFERENCE_INIT_STATE");vr::EVRInitError error=vr::VRInitError_None;
  system_=vr::VR_Init(&error,type);need(error==vr::VRInitError_None&&system_,"OPENVR_BACKGROUND_UNAVAILABLE");
 }
 void shutdown()override{if(system_){system_=nullptr;vr::VR_Shutdown();}}
 std::uint64_t generation()override{ready();return vr::VR_GetInitToken();}
 bool event(vr::VREvent_t& value)override{ready();return system_->PollNextEvent(&value,sizeof(value));}
 vr::HmdMatrix34_t standing()override{ready();return system_->GetRawZeroPoseToStandingAbsoluteTrackingPose();}
 void poses(vr::ETrackingUniverseOrigin origin,float prediction,std::array<vr::TrackedDevicePose_t,vr::k_unMaxTrackedDeviceCount>& values)override{ready();system_->GetDeviceToAbsoluteTrackingPose(origin,prediction,values.data(),static_cast<std::uint32_t>(values.size()));}
 vr::ETrackedDeviceClass deviceClass(vr::TrackedDeviceIndex_t index)override{ready();return system_->GetTrackedDeviceClass(index);}
 vr::ETrackedControllerRole role(vr::TrackedDeviceIndex_t index)override{ready();return system_->GetControllerRoleForTrackedDeviceIndex(index);}
 std::string serial(vr::TrackedDeviceIndex_t index)override{
  ready();std::array<char,4096> value{};vr::ETrackedPropertyError error=vr::TrackedProp_Success;
  const auto length=system_->GetStringTrackedDeviceProperty(index,vr::Prop_SerialNumber_String,value.data(),static_cast<std::uint32_t>(value.size()),&error);
  need(error==vr::TrackedProp_Success&&length>1&&length<=value.size()&&value[length-1]==0,"REFERENCE_SERIAL_UNAVAILABLE");return {value.data(),length-1};
 }
 std::uint64_t universe()override{
  ready();need(system_->GetTrackedDeviceClass(vr::k_unTrackedDeviceIndex_Hmd)==vr::TrackedDeviceClass_HMD,"REFERENCE_UNIVERSE_UNAVAILABLE");
  vr::ETrackedPropertyError error=vr::TrackedProp_Success;const auto value=system_->GetUint64TrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,vr::Prop_CurrentUniverseId_Uint64,&error);need(error==vr::TrackedProp_Success,"REFERENCE_UNIVERSE_UNAVAILABLE");return value;
 }
};
class MirrorUdp final:public CalibrationMirrorTransport {
 std::unique_ptr<Udp> socket_;
public:
 void open(std::uint16_t port)override{need(!socket_&&port!=0,"MIRROR_OPEN_STATE");socket_=std::make_unique<Udp>(port);}
 std::optional<Datagram> receive()override{need(bool(socket_),"MIRROR_NOT_OPEN");return socket_->receive();}
 void shutdown()override{socket_.reset();}
};
}
std::unique_ptr<CaptureProvider> makeProductionCaptureProvider(){return std::make_unique<RuntimeCaptureProvider>(std::make_unique<OpenVrReference>(),std::make_unique<MirrorUdp>());}
int calibrationLocalDispatch(int argc,char** argv){
 if(argc!=2)return -1;std::string mode=argv[1];
 if(mode=="--hil-runtime-path"){
  std::array<char,32768> runtime{};std::uint32_t required=0;
  bool available=vr::VR_GetRuntimePath(runtime.data(),static_cast<std::uint32_t>(runtime.size()),&required);
  Json out={{"available",available&&required<=runtime.size()},{"runtime_path",available&&required<=runtime.size()?runtime.data():""},{"runtime_initialized",false}};
  for(const char* key:{"USERPROFILE","LOCALAPPDATA","TEMP","HOME"}){char value[32768]{};auto n=GetEnvironmentVariableA(key,value,32768);out[key]=n&&n<32768?value:"";}
  std::cout<<out.dump()<<'\n';return 0;
 }
 if(mode=="--local-api-version"){std::cout<<productionCalibrationVersion().dump()<<'\n';return 0;}
 if(mode!="--local-api")return -1;
 try{
  wchar_t path[32768];DWORD length=GetModuleFileNameW(nullptr,path,32768);need(length&&length<32768,"HELPER_PATH_UNAVAILABLE");
  wchar_t local[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);need(n&&n<32768,"OWNER_APPDATA_UNAVAILABLE");
  SessionEnvironment env;env.draftRoot=std::filesystem::path(local)/"MonakaBridge"/"CalibrationDrafts";env.helperPath=std::filesystem::path(path).u8string();env.helperSha256=sha256(readConfigBytes(std::filesystem::path(path)));
  // Ordinary measurement/save does not load external quality approval policy.
  auto provider=makeProductionCaptureProvider();return runLocalApi(*provider,std::move(env));
 }catch(const std::exception& e){std::cout<<Json{{"status","UNAVAILABLE"},{"reason",diagnostic(e)}}.dump()<<'\n';return 2;}
}
} // namespace mb::local_calibration

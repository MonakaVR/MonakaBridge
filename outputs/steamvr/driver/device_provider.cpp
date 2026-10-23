#include "device_provider.hpp"
#include "driver_log.hpp"
namespace mb::steamvr {
// Task2 lifecycle retained: dynamic add; registered objects live until provider Cleanup.
vr::EVRInitError DeviceProvider::Init(vr::IVRDriverContext* context){
 VR_INIT_SERVER_DRIVER_CONTEXT(context);
 // The pinned SDK checks the non-null property helper, not its raw interface.
 // Refuse a missing Properties interface before opening the input socket.
 if(!vr::VRPropertiesRaw())return vr::VRInitError_Init_InterfaceNotFound;
 try{
  // A deployment setting also permits isolated ephemeral-port Mock Hosts.
  vr::EVRSettingsError error=vr::VRSettingsError_None;
  auto port=vr::VRSettings()->GetInt32("driver_monaka_bridge","input_port",&error);
  if(error==vr::VRSettingsError_UnsetSettingHasNoDefault)port=29812;
  else if(error!=vr::VRSettingsError_None)throw std::runtime_error("input_port setting unavailable");
  if(port<1||port>65535)throw std::runtime_error("input_port outside 1..65535");
  receiver_=std::make_unique<Udp>(static_cast<std::uint16_t>(port));
 }catch(const std::exception& e){DriverLog("monaka_bridge: %s",e.what());return vr::VRInitError_Driver_Unknown;}
 try{presentation_=Presentation::load(currentDriverPackage());}catch(const std::exception& e){DriverLog("monaka_bridge: presentation unavailable: %s",e.what());presentation_=Presentation{};}
 return vr::VRInitError_None;
}
void DeviceProvider::Cleanup(){devices_.clear();receiver_.reset();feed_=MtpFeed{};presentation_=Presentation{};VR_CLEANUP_SERVER_DRIVER_CONTEXT();}
const char* const* DeviceProvider::GetInterfaceVersions(){return vr::k_InterfaceVersions;}
bool DeviceProvider::ShouldBlockStandbyMode(){return false;}
void DeviceProvider::EnterStandby(){}void DeviceProvider::LeaveStandby(){}
void DeviceProvider::RunFrame(){
 if(!receiver_)return;
 for(int i=0;i<128;++i){auto d=receiver_->receive();if(!d)break;feed_.receive(d->bytes,monotonicNs(),d->peer);}
 for(auto& [key,sample]:feed_.devices){
  if(devices_.count(key)||!sample.pose)continue;
  auto serial=runtimeSerial(key.first,key.second);auto device=std::make_unique<TrackerDevice>(serial,presentation_.eligible(serial,&sample,monotonicNs())?PicoRenderModel:"");
  if(vr::VRServerDriverHost()->TrackedDeviceAdded(serial.c_str(),vr::TrackedDeviceClass_GenericTracker,device.get()))devices_.emplace(key,std::move(device));
 }
 auto now=monotonicNs();for(auto& [key,device]:devices_){auto s=feed_.devices.find(key);auto sample=s==feed_.devices.end()?nullptr:&s->second;device->Update(sample,now,presentation_.eligible(device->Serial(),sample,now));}
 vr::VREvent_t event{};while(vr::VRServerDriverHost()->PollNextEvent(&event,sizeof(event))){}
}
}

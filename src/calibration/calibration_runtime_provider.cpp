#include "monaka_bridge/calibration_runtime_provider.hpp"
#include "monaka_bridge/world_calibration.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mb::local_calibration {
namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
bool finiteMatrix(const vr::HmdMatrix34_t& m){for(const auto& row:m.m)for(float x:row)if(!std::isfinite(x))return false;return true;}
std::uint32_t finiteValueKey(float value){
 std::uint32_t bits=0;static_assert(sizeof(bits)==sizeof(value));std::memcpy(&bits,&value,sizeof(bits));
 // For finite IEEE-754 binary32 values only signed zero has two encodings.
 // Bit comparison avoids treating a subnormal as zero under a caller's DAZ/FTZ.
 return (bits&0x7fffffffU)==0?0:bits;
}
std::string standingKey(const vr::HmdMatrix34_t& m,std::uint64_t universe){
 std::string raw;for(const auto& row:m.m)for(float value:row){const auto bits=finiteValueKey(value);for(unsigned i=0;i<4;++i)raw.push_back(char((bits>>(i*8))&255));}
 return "standing-f32-exact:"+sha256(raw)+":"+std::to_string(universe);
}
}
Rigid calibrationReferenceRigid(const vr::HmdMatrix34_t& m){
 need(finiteMatrix(m),"NONFINITE_REFERENCE_MATRIX");
 const double determinant=double(m.m[0][0])*(double(m.m[1][1])*m.m[2][2]-double(m.m[1][2])*m.m[2][1])-double(m.m[0][1])*(double(m.m[1][0])*m.m[2][2]-double(m.m[1][2])*m.m[2][0])+double(m.m[0][2])*(double(m.m[1][0])*m.m[2][1]-double(m.m[1][1])*m.m[2][0]);
 need(determinant>0,"IMPROPER_REFERENCE_MATRIX");
 const Vec translation{m.m[0][3],m.m[1][3],m.m[2][3]};
 std::vector<PointCorrespondence> axes{{{0,0,0},translation}};
 for(int k=0;k<3;++k){Vec unit{},column{};unit[k]=1;for(int j=0;j<3;++j)column[j]=m.m[j][k];axes.push_back({unit,add(column,translation)});}
 const auto result=solveRigidAlignment(axes);
 need(result.maxResidual<=.001,"NONRIGID_REFERENCE_MATRIX");return result.transform;
}
bool sameStandingValues(const vr::HmdMatrix34_t& a,const vr::HmdMatrix34_t& b){
 if(!finiteMatrix(a)||!finiteMatrix(b))return false;
 for(int i=0;i<3;++i)for(int j=0;j<4;++j)if(finiteValueKey(a.m[i][j])!=finiteValueKey(b.m[i][j]))return false;return true;
}
RuntimeCaptureProvider::RuntimeCaptureProvider(std::unique_ptr<CalibrationReferenceApi> api,std::unique_ptr<CalibrationMirrorTransport> transport,Clock clock,Sleeper sleeper)
 :api_(std::move(api)),transport_(std::move(transport)),clock_(std::move(clock)),sleep_(std::move(sleeper)){
 need(bool(api_)&&bool(transport_)&&bool(clock_),"CAPTURE_DEPENDENCY_REQUIRED");
 if(!sleep_)sleep_=[](auto duration){std::this_thread::sleep_for(duration);};
}
void RuntimeCaptureProvider::validateConfiguration(const ProviderOpenOptions& o)const{
 need(o.mirrorAddress()=="127.0.0.1"&&o.mirrorPort()!=0,"ASSOCIATION_UNSUPPORTED");
 need(o.referenceRole()=="left"||o.referenceRole()=="right"||o.referenceRole()=="hmd","REFERENCE_ROLE_UNSUPPORTED");
}
void RuntimeCaptureProvider::requireOwner()const{need(started_&&!closed_&&std::this_thread::get_id()==owner_,"CAPTURE_OWNER_REQUIRED");}
vr::TrackedDeviceIndex_t RuntimeCaptureProvider::selectReference(){
 const auto& selected=configuration()->referenceRole();vr::TrackedDeviceIndex_t index=vr::k_unTrackedDeviceIndexInvalid;
 for(vr::TrackedDeviceIndex_t i=0;i<vr::k_unMaxTrackedDeviceCount;++i){
  const auto cls=api_->deviceClass(i);
  const bool match=selected=="hmd"?cls==vr::TrackedDeviceClass_HMD:cls==vr::TrackedDeviceClass_Controller&&api_->role(i)==(selected=="left"?vr::TrackedControllerRole_LeftHand:vr::TrackedControllerRole_RightHand);
  if(match){need(index==vr::k_unTrackedDeviceIndexInvalid,"REFERENCE_AMBIGUOUS");index=i;}
 }
 need(index!=vr::k_unTrackedDeviceIndexInvalid,"REFERENCE_UNAVAILABLE");return index;
}
void RuntimeCaptureProvider::events(bool enforce){
 vr::VREvent_t event{};
 for(int i=0;i<=128;++i){
  if(!api_->event(event))return;need(i<128,"REFERENCE_EVENT_OVERFLOW");if(!enforce)continue;
  switch(event.eventType){
  case vr::VREvent_ChaperoneDataHasChanged:case vr::VREvent_ChaperoneUniverseHasChanged:case vr::VREvent_ChaperoneSettingsHaveChanged:case vr::VREvent_ChaperoneFlushCache:case vr::VREvent_ChaperoneRoomSetupStarting:case vr::VREvent_ChaperoneRoomSetupCommitted:case vr::VREvent_StandingZeroPoseReset:
   throw std::runtime_error("STANDING_EVENT_CHANGED");
  case vr::VREvent_Quit:throw std::runtime_error("REFERENCE_RUNTIME_EXIT");
  case vr::VREvent_TrackedDeviceActivated:case vr::VREvent_TrackedDeviceDeactivated:case vr::VREvent_TrackedDeviceUpdated:case vr::VREvent_TrackedDeviceRoleChanged:
   if(event.trackedDeviceIndex==reference_||event.trackedDeviceIndex==vr::k_unTrackedDeviceIndex_Hmd)throw std::runtime_error("REFERENCE_CONNECTION_EVENT");break;
  default:break;
  }
 }
}
ProviderBatch RuntimeCaptureProvider::poll(){
 requireOwner();events(referenceFrozen_);
 const auto index=selectReference();const auto generation=api_->generation(),universe=api_->universe();
 const auto serial=api_->serial(index);need(!serial.empty()&&serial.size()<=4096,"REFERENCE_SERIAL_UNAVAILABLE");
 auto beforeFrame=api_->standing();calibrationReferenceRigid(beforeFrame);
 if(referenceFrozen_)need(index==reference_&&generation==witness_.referenceGeneration&&serial==witness_.referenceSerial&&universe==universe_&&sameStandingValues(beforeFrame,standing_),"REFERENCE_FRAME_CHANGED");
 const auto before=clock_();need(before>=0&&(lastPoll_<0||before>=lastPoll_),"MONOTONIC_CLOCK_ROLLBACK");
 need(lastPoll_<0||before-lastPoll_<=50000000,"REFERENCE_POLL_STALLED");lastPoll_=before;
 std::array<vr::TrackedDevicePose_t,vr::k_unMaxTrackedDeviceCount> poses{};
 api_->poses(vr::TrackingUniverseStanding,0.0F,poses);const auto after=clock_();need(after>=before,"MONOTONIC_CLOCK_ROLLBACK");
 need(after-before<=50000000,"REFERENCE_QUERY_STALLED");
 auto afterFrame=api_->standing();calibrationReferenceRigid(afterFrame);
 need(sameStandingValues(beforeFrame,afterFrame)&&selectReference()==index&&api_->serial(index)==serial&&api_->universe()==universe&&api_->generation()==generation,"REFERENCE_QUERY_FRAME_CHANGED");
 if(!referenceFrozen_){reference_=index;standing_=beforeFrame;universe_=universe;witness_.referenceRole=configuration()->referenceRole();witness_.referenceSerial=serial;witness_.referenceGeneration=generation;witness_.standing=standingKey(standing_,universe_);referenceFrozen_=true;}
 events(true);const auto& reference=poses[index];need(reference.bDeviceIsConnected,"REFERENCE_DISCONNECTED");
 ProviderBatch batch;batch.referenceReceiptNs=after;batch.referenceValid=reference.bPoseIsValid&&reference.eTrackingResult==vr::TrackingResult_Running_OK&&after-before<=20000000;
 const auto rigid=batch.referenceValid?calibrationReferenceRigid(reference.mDeviceToAbsoluteTracking):Rigid{};
 batch.references.push_back({before+(after-before)/2,rigid,batch.referenceValid});
 for(int i=0;i<=128;++i){
  auto datagram=transport_->receive();if(!datagram)break;need(i<128,"MIRROR_BATCH_OVERFLOW");
  const auto receipt=clock_();need(receipt>=after,"MONOTONIC_CLOCK_ROLLBACK");
  c1::Envelope value;c1::Error error;if(!c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(datagram->bytes.data()),datagram->bytes.size(),value,error))continue;
  auto selected=[&](const auto& p){return p.source_id==witness_.source&&p.device_id==witness_.device;};
  auto seed=[&](const auto& p){if(witness_.session.empty()){witness_.session=p.session_id;witness_.clock=p.clock_id;witness_.peer=datagram->peer;}};
  bool keep=false;if(auto p=std::get_if<c1::TrackerObservation>(&value)){keep=selected(*p);if(keep)seed(*p);}else if(auto p=std::get_if<c1::ObservationDeviceState>(&value)){keep=selected(*p);if(keep)seed(*p);}
  if(keep)batch.frames.push_back({std::move(value),datagram->peer,receipt});
 }
 batch.identity=witness_;return batch;
}
ProviderBatch RuntimeCaptureProvider::discover(const std::atomic<bool>& cancel){
 need(configuration()!=nullptr,"PROVIDER_CONFIGURATION_REQUIRED");need(!started_,"PROVIDER_DISCOVERY_ALREADY_STARTED");
 need(configuration()->capabilityProfile().has_value(),"EXPLICIT_DECLARATION_REQUIRED");
 started_=true;owner_=std::this_thread::get_id();need(!cancel,"CANCELLED");
 const auto& frozen=configuration()->measurementTarget();const auto& b=frozen.target().binding;
 witness_.source=b.source;witness_.device=b.device;witness_.space=b.inputSpace;witness_.revision=b.inputRevision;witness_.convention=frozen.target().profile.convention;
 const auto start=clock_();api_->initialize(vr::VRApplication_Background);need(!cancel,"CANCELLED");
 // Failed Background init cannot bind a mirror port. No SDK/socket is touched
 // before this explicit discover on the owner thread.
 transport_->open(configuration()->mirrorPort());events(false);std::vector<Frame> retained;
 while(!cancel){
  need(clock_()-start<10*second,"DISCOVER_TIMEOUT");auto batch=poll();
  need(retained.size()+batch.frames.size()<=128,"DISCOVERY_BATCH_OVERFLOW");for(auto& f:batch.frames)retained.push_back(std::move(f));
  const bool pose=std::any_of(retained.begin(),retained.end(),[](const auto& f){return std::holds_alternative<c1::TrackerObservation>(f.envelope);});
  if(pose){batch.frames=std::move(retained);return batch;}sleep_(std::chrono::milliseconds(8));
 }
 throw std::runtime_error("CANCELLED");
}
ProviderBatch RuntimeCaptureProvider::guard(){return poll();}
void RuntimeCaptureProvider::capture(const std::function<void(const ProviderBatch&)>& sink,const std::atomic<bool>& cancel){
 requireOwner();const auto start=clock_();while(!cancel&&clock_()-start<10*second){sink(poll());sleep_(std::chrono::milliseconds(8));}need(!cancel,"CANCELLED");
}
void RuntimeCaptureProvider::shutdown(){
 if(!started_||closed_)return;need(std::this_thread::get_id()==owner_,"CAPTURE_OWNER_REQUIRED");closed_=true;
 std::exception_ptr error;try{transport_->shutdown();}catch(...){error=std::current_exception();}try{api_->shutdown();}catch(...){if(!error)error=std::current_exception();}if(error)std::rethrow_exception(error);
}
Json productionCalibrationVersion(){return {{"api_version",1},{"software_policy",1},{"capture_supported",true},{"static_supported",false},{"production_capture","OPENVR_BACKGROUND_MIRROR"},{"standing_policy","EXACT_FINITE_NUMERIC"},{"runtime_validation","NOT_RUN"},{"output_approval",false},{"gui_contract",1},{"features",Json::array({"idle_invalidation","phase_events","witness_details","immutable_result_commit"})}};}
} // namespace mb::local_calibration

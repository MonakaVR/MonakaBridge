#pragma once
#include "calibration_fixture_provider.hpp"
#include "monaka_bridge/calibration_runtime_provider.hpp"
#include "monaka_bridge/calibration_capture_monitor.hpp"
#include <deque>

namespace runtime_fixture {
namespace lc=mb::local_calibration;
inline vr::HmdMatrix34_t matrix(const mb::Rigid& pose){vr::HmdMatrix34_t m{};for(int k=0;k<3;++k){mb::Vec unit{};unit[k]=1;const auto v=mb::rotate(pose.rotation,unit);for(int j=0;j<3;++j)m.m[j][k]=float(v[j]);}for(int j=0;j<3;++j)m.m[j][3]=float(pose.translation[j]);return m;}
struct State {
 std::atomic<std::int64_t>& time;std::mutex mutex;std::deque<vr::VREvent_t> events;std::deque<mb::Datagram> datagrams;
 bool generate=true,valid=true,connected=true,initialized=false,bound=false,failInit=false,failBind=false,ambiguous=false;
 std::string serial="fake-controller",peer="127.0.0.1:41000";std::uint64_t generation=1,universe=7;std::int64_t sequence=0,queryDelay=0;
 unsigned initCalls=0,bindCalls=0,poseCalls=0,shutdownCalls=0,transportCloses=0,eventCalls=0;bool packetSent=false;
 std::uint16_t expectedMirrorPort=49123; // Test-only transport, never binds a socket.
 vr::ETrackedControllerRole controllerRole=vr::TrackedControllerRole_LeftHand;
 vr::EVRApplicationType application=vr::VRApplication_Other;vr::ETrackingUniverseOrigin origin=vr::TrackingUniverseRawAndUncalibrated;float prediction=-1;
 vr::HmdMatrix34_t frame=matrix({});std::thread::id owner;std::function<void()> poseHook;
 mb::Rigid world{mb::normalized({.2,-.3,.1,.9}),{1,2,-.5}},extrinsic{mb::normalized({.3,.1,-.2,.9}),{.1,.05,-.2}};
 explicit State(std::atomic<std::int64_t>& t):time(t){}
 void thread(){CHECK(owner==std::this_thread::get_id());}
 mb::Rigid source(){double t=double(time.load()-100*lc::second)/1e9;return {mb::mul(mb::mul(fixture::axis(0,.8*std::sin(1.3*t)),fixture::axis(1,.9*std::sin(.9*t))),fixture::axis(2,.7*std::sin(1.7*t))),{.45*std::sin(t),.35*std::sin(1.4*t),.30*std::sin(.7*t)}};}
 mb::Datagram packet(){auto p=sample("pico",++sequence);auto pose=source();p.position=pose.translation;p.orientation=pose.rotation;p.timestamp_ns=time;p.sent_at_ns=time;std::string raw;mb::c1::Error e;CHECK(mb::c1::EncodeEnvelope(p,raw,e));return {raw,peer};}
 void event(std::uint32_t type,vr::TrackedDeviceIndex_t index=vr::k_unTrackedDeviceIndexInvalid){vr::VREvent_t e{};e.eventType=type;e.trackedDeviceIndex=index;events.push_back(e);}
};
struct Api final:lc::CalibrationReferenceApi {
 State& s;explicit Api(State& value):s(value){}
 void initialize(vr::EVRApplicationType app)override{s.owner=std::this_thread::get_id();++s.initCalls;s.application=app;if(s.failInit)throw std::runtime_error("OPENVR_BACKGROUND_UNAVAILABLE");s.initialized=true;}
 void shutdown()override{if(!s.initCalls)return;s.thread();++s.shutdownCalls;s.initialized=false;}
 std::uint64_t generation()override{s.thread();return s.generation;}
 bool event(vr::VREvent_t& e)override{s.thread();++s.eventCalls;if(s.events.empty())return false;e=s.events.front();s.events.pop_front();return true;}
 vr::HmdMatrix34_t standing()override{s.thread();return s.frame;}
 void poses(vr::ETrackingUniverseOrigin origin,float prediction,std::array<vr::TrackedDevicePose_t,vr::k_unMaxTrackedDeviceCount>& out)override{
  s.thread();++s.poseCalls;s.origin=origin;s.prediction=prediction;if(s.poseHook)s.poseHook();for(auto index:{0,1}){auto& p=out[index];p.bDeviceIsConnected=s.connected;p.bPoseIsValid=s.valid;p.eTrackingResult=vr::TrackingResult_Running_OK;p.mDeviceToAbsoluteTracking=matrix(mb::composeRigid(mb::composeRigid(s.world,s.source()),s.extrinsic));}s.time+=s.queryDelay;
 }
 vr::ETrackedDeviceClass deviceClass(vr::TrackedDeviceIndex_t i)override{s.thread();return i==0?vr::TrackedDeviceClass_HMD:(i==1||(i==2&&s.ambiguous)?vr::TrackedDeviceClass_Controller:vr::TrackedDeviceClass_Invalid);}
 vr::ETrackedControllerRole role(vr::TrackedDeviceIndex_t i)override{s.thread();return i==1||i==2?s.controllerRole:vr::TrackedControllerRole_Invalid;}
 std::string serial(vr::TrackedDeviceIndex_t)override{s.thread();return s.serial;}
 std::uint64_t universe()override{s.thread();return s.universe;}
};
struct Transport final:lc::CalibrationMirrorTransport {
 State& s;explicit Transport(State& value):s(value){}
 void open(std::uint16_t port)override{s.thread();CHECK(port==s.expectedMirrorPort);++s.bindCalls;if(s.failBind)throw std::runtime_error("MIRROR_BIND_FAILED");s.bound=true;}
 std::optional<mb::Datagram> receive()override{s.thread();CHECK(s.bound);if(!s.datagrams.empty()){auto p=s.datagrams.front();s.datagrams.pop_front();return p;}if(!s.generate)return {};if(s.packetSent){s.packetSent=false;return {};}s.packetSent=true;return s.packet();}
 void shutdown()override{if(!s.bindCalls)return;s.thread();++s.transportCloses;s.bound=false;}
};
inline std::unique_ptr<lc::RuntimeCaptureProvider> provider(State& s,bool paced=false){return std::make_unique<lc::RuntimeCaptureProvider>(std::make_unique<Api>(s),std::make_unique<Transport>(s),[&]{return s.time.load();},[&,paced](auto ns){s.time+=ns.count();if(paced){fixture::CapturePacer pacer;pacer.wait();}});}
} // namespace runtime_fixture

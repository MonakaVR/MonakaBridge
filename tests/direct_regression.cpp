#include "helpers.hpp"
#include "direct_pose.hpp"
#include "../build/upstream/task2/outputs/steamvr/driver/tracker_device.hpp"
#include <iostream>
#include "standalone_driver_context.hpp"
int main()try{
 StandaloneDriverContext context;
 pico_ot::steamvr::TrackerDevice legacy("legacy");pico_ot::bridge::ReceivedTrackerState old{};old.hasPoseSequence=true;old.lastPoseSequence=1;old.senderSessionId=1;old.lastPoseReceivePcNs=10000000;old.posePcMonotonicNs=9000000;
 old.pose.positionValid=true;old.pose.orientationSamplePresent=true;old.pose.positionMeters={1,2,3};old.pose.orientationXyzw={.5f,.5f,.5f,.5f};old.pose.linearVelocityMetersPerSec={2,3,4};old.pose.observedMonotonicNs=9000000;
 legacy.SetWorldTranslation(10,20,30);legacy.Update(&old,false,10000000);auto baseline=legacy.GetPose();
 auto c=config();c.bindings.at({"pico","device"}).world.translation={10,20,30};mb::Bridge bridge(c,SB);auto p=sample();p.linear_velocity=mb::c1::Derivative{{2,3,4},"space","measured"};p.capabilities.push_back("linear_velocity");bridge.receive(wire(p),"p",10000000);
 auto output=*bridge.logical().at({"pico","device"}).pose;mb::DirectSample sample;sample.pose=output;sample.fixedTime=9000000;
 auto common=mb::steamvr::directPose(&sample,10000000);
 for(int i=0;i<3;++i){CHECK(near(common.vecPosition[i],baseline.vecPosition[i]+baseline.vecWorldFromDriverTranslation[i]));CHECK(near(common.vecVelocity[i],baseline.vecVelocity[i]));}
 CHECK(near(common.qRotation.x,baseline.qRotation.x)&&near(common.qRotation.y,baseline.qRotation.y)&&near(common.qRotation.z,baseline.qRotation.z)&&near(common.qRotation.w,baseline.qRotation.w));CHECK(near(common.poseTimeOffset,baseline.poseTimeOffset));CHECK(common.poseIsValid==baseline.poseIsValid);
 // A second fresh sample exercises the migrated shortest-arc angular velocity.
 auto next=p;next.sequence=1;next.orientation=mb::mul(mb::Quat{0,std::sin(.005),0,std::cos(.005)},*p.orientation);next.timestamp_ns+=10000000;next.sent_at_ns+=10000000;
 old.lastPoseSequence=2;old.lastPoseReceivePcNs=20000000;old.posePcMonotonicNs=19000000;old.pose.observedMonotonicNs=19000000;
 const auto& q=*next.orientation;old.pose.orientationXyzw={q[0],q[1],q[2],q[3]};legacy.Update(&old,false,20000000);
 bridge.receive(wire(next),"p",20000000);auto derived=*bridge.logical().at({"pico","device"}).pose;CHECK(derived.angular_velocity);for(int i=0;i<3;++i)CHECK(near((*derived.angular_velocity)[i],legacy.GetPose().vecAngularVelocity[i]));
 sample.pose->modality="rotation_only";sample.pose->validity.position=false;sample.pose->position.reset();common=mb::steamvr::directPose(&sample,10000000);CHECK(!common.poseIsValid&&common.deviceIsConnected&&common.result==vr::TrackingResult_Fallback_RotationOnly);CHECK(near(common.qRotation.w,output.orientation->at(3)));
 common=mb::steamvr::directPose(&sample,600000000);CHECK(!common.deviceIsConnected&&!common.poseIsValid);
 legacy.RequestOrientationZero();legacy.Update(&old,false,20000000);CHECK(near(legacy.GetPose().qRotation.w,1));auto zeroed=mb::orientationZero(*derived.orientation,*derived.orientation);CHECK(near(zeroed[3],1));
 legacy.RequestPositionZero();legacy.Update(&old,false,20000000);for(int i=0;i<3;++i)CHECK(near(legacy.GetPose().vecPosition[i],0));
 CHECK(context.logRequests==1&&context.logMessages>0&&context.unexpectedRequests==0);
 std::cout<<"PASS actual Task2 TrackerDevice vs common calibrated Direct/MTP pose, velocity, quaternion, age and zero diagnostics; mock logger only, runtime interface requests=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

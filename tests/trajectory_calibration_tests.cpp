#include "helpers.hpp"
#include "monaka_bridge/trajectory_calibration.hpp"
#include <iostream>
#include <random>

namespace {
mb::Quat axis(int i,double a){mb::Quat q{0,0,0,std::cos(a/2)};q[i]=std::sin(a/2);return q;}
mb::Rigid motion(double t){return {mb::mul(mb::mul(axis(0,.8*std::sin(1.3*t)),axis(1,.9*std::sin(.9*t))),axis(2,.7*std::sin(1.7*t))),{.45*std::sin(t),.35*std::sin(1.4*t),.30*std::sin(.7*t)}};}
const mb::Rigid W{mb::normalized({.2,-.3,.1,.9}),{1,2,-.5}}, E{mb::normalized({.3,.1,-.2,.9}),{.1,.05,-.2}};
std::vector<mb::TrajectoryPair> synthetic(double noise=0,bool outliers=false,bool signs=false){
    std::mt19937 rng(103);std::normal_distribution<double> normal(0,noise);std::vector<mb::TrajectoryPair> pairs;
    for(int i=0;i<=500;++i){auto v=motion(i*.02),r=mb::composeRigid(mb::composeRigid(W,v),E);
        if(noise){for(auto& x:r.translation)x+=normal(rng);for(auto& x:v.translation)x+=normal(rng);for(int k=0;k<3;++k){r.rotation=mb::mul(r.rotation,axis(k,normal(rng)));v.rotation=mb::mul(v.rotation,axis(k,normal(rng)));}}
        if(outliers&&i%31==0){r.translation=mb::add(r.translation,{.2,-.15,.3});r.rotation=mb::mul(r.rotation,axis(1,.45));}
        if(signs){if(i%2)for(auto& x:v.rotation)x=-x;if(i%3)for(auto& x:r.rotation)x=-x;}
        std::int64_t time=1000000000LL+i*20000000LL;pairs.push_back({{time,v,true},{time,r,true}});
    }return pairs;
}
void close(const mb::Rigid& a,const mb::Rigid& b,double tolerance){CHECK(mb::norm(mb::add(a.translation,mb::scale(b.translation,-1)))<tolerance);CHECK(mb::rotationDistance(a.rotation,b.rotation)<tolerance);}
template<class F>void reject(F f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}CHECK(failed);}
}
int main()try{
    auto exact=synthetic();auto result=mb::solveTrajectory(exact,1);close(result.world,W,1e-7);close(result.extrinsic,E,1e-7);CHECK(result.residuals.maxPosition<1e-7&&result.residuals.maxOrientation<1e-7);
    auto sign=mb::solveTrajectory(synthetic(0,false,true),1);close(sign.world,W,1e-7);close(sign.extrinsic,E,1e-7);
    auto noisy=mb::solveTrajectory(synthetic(.0015),1);close(noisy.world,W,.015);close(noisy.extrinsic,E,.015);CHECK(noisy.residuals.rmsPosition<.01);
    auto robust=mb::solveTrajectory(synthetic(.001,true),1);close(robust.world,W,.02);close(robust.extrinsic,E,.02);CHECK(robust.inliers<robust.pairs&&robust.inliers>robust.pairs*.9);CHECK(robust.allResiduals.maxPosition>.1&&robust.residuals.maxPosition<.05);
    for(int mode=0;mode<4;++mode){auto degenerate=exact;for(std::size_t i=0;i<degenerate.size();++i){auto& v=degenerate[i].source.pose;
        if(mode==0)v={mb::Quat{0,0,0,1},{std::sin(i*.02),0,0}};
        if(mode==1)v.rotation=axis(1,std::sin(i*.02));
        if(mode==2)v={};
        if(mode==3)v.translation[2]=0; // Planar figure-eight is not enough.
        degenerate[i].reference.pose=mb::composeRigid(mb::composeRigid(W,v),E);
    }reject([&]{mb::solveTrajectory(degenerate,1);});}
    reject([&]{mb::solveTrajectory(exact,.3);});auto shortRun=exact;shortRun.resize(100);reject([&]{mb::solveTrajectory(shortRun,1);});
    auto manyOutliers=exact;for(std::size_t i=0;i<manyOutliers.size();i+=2)manyOutliers[i].reference.pose.translation[0]+=1;reject([&]{mb::solveTrajectory(manyOutliers,1);});
    std::vector<mb::TimedCalibrationPose> source,reference;
    for(auto& p:exact){source.push_back(p.source);reference.push_back(p.reference);}
    for(std::size_t i=0;i<reference.size();++i)reference[i].timeNs+=1000000;
    auto paired=mb::pairTrajectory(source,reference,{});CHECK(paired.size()==source.size());
    reference.erase(reference.begin()+100,reference.begin()+200);paired=mb::pairTrajectory(source,reference,{});CHECK(paired.size()<source.size()-90);
    reference.clear();for(std::int64_t t=1000000000;t<=11000000000;t+=31000000)reference.push_back({t,motion(double(t-1000000000)/1e9),true});
    paired=mb::pairTrajectory(source,reference,{});CHECK(paired.size()>490); // Unequal rates, bounded nearest matching.
    source[10].valid=false;reference[20].valid=false;paired=mb::pairTrajectory(source,reference,{});for(auto& p:paired)CHECK(p.source.timeNs!=source[10].timeNs&&p.reference.timeNs!=reference[20].timeNs);
    auto stalePairs=exact;stalePairs[0].reference.timeNs+=100000000;reject([&]{mb::solveTrajectory(stalePairs,1);});

    auto c=config();auto& b=c.bindings.at({"pico","device"});b.mount.translation={1,2,3};b.mount.rotation=axis(2,.5);
    auto target=mb::selectWorldCalibrationTarget(c,"pico","device","logical-pico","native",1);
    mb::TrajectoryRecorder recorder(c,target,"controller-serial");auto p=sample();p.orientation=mb::Quat{0,0,0,1};
    CHECK(recorder.observation(p,1000000000));CHECK(recorder.sourceSamples().back().timeNs==999000000);
    CHECK(recorder.sourceSamples().back().pose.translation==*p.position); // Profile only: mount not applied.
    CHECK(!recorder.observation(p,2000000000)&&recorder.sourceSamples().size()==1);
    ++p.sequence;p.sent_at_ns=p.timestamp_ns+100000000;CHECK(!recorder.observation(p,2100000000));
    ++p.sequence;p.sent_at_ns=p.timestamp_ns;p.tracking_state="lost";CHECK(!recorder.observation(p,2200000000));CHECK(!recorder.sourceSamples().back().valid);
    auto next=p;next.coordinate_space.revision++;reject([&]{recorder.observation(next,2300000000);});reject([&]{recorder.observation(p,2400000000);});reject([&]{recorder.solve();});
    for(int change=0;change<4;++change){mb::TrajectoryRecorder r(c,target,"controller-serial");auto bad=sample();r.observation(bad,1000000000);
        if(change==0)bad.device_id="other";if(change==1)bad.source_id="other";if(change==2)bad.session_id=S2;if(change==3)bad.clock_id=S2;
        reject([&]{r.observation(bad,2000000000);});}
    for(int change=0;change<3;++change){mb::TrajectoryRecorder r(c,target,"controller-serial");auto changed=c;if(change==0)changed.revision++;if(change==1)changed.bridgeId="other-publisher";if(change==2)changed.bindings.at({"pico","device"}).tracker="other";reject([&]{r.checkConfig(changed);});reject([&]{r.checkConfig(c);});}
    {mb::TrajectoryRecorder r(c,target,"controller-serial");reject([&]{r.reference({1,{},true},"changed-reference");});}
    {mb::TrajectoryRecorder r(c,target,"controller-serial");auto s=state(sample());s.coordinate_space.revision++;reject([&]{r.deviceState(s);});}
    {auto o=mb::TrajectoryOptions{};o.maxSamples=80;mb::TrajectoryRecorder r(c,target,"controller-serial",o);for(int i=0;i<80;++i)r.reference({i,{},true},"controller-serial");reject([&]{r.reference({81,{},true},"controller-serial");});}
    auto candidate=mb::worldCalibrationCandidate(c,target,result.world,true);CHECK(candidate.revision==c.revision+1);CHECK(candidate.bindings.at({"pico","device"}).mount.translation==b.mount.translation);CHECK(candidate.bindings.at({"pico","device"}).spaceApproved==b.spaceApproved);
    CHECK(mb::worldCalibrationCandidate(c,target,result.world,false).revision==c.revision);
    // Exercise the fixed codec -> recorder -> time pairing -> solver boundary,
    // with actual invalid/lost packets and a nonzero mount that must stay unused.
    auto captureConfig=c;captureConfig.profiles.at("pico").quaternionAxes={1,2,3,4};
    auto captureTarget=mb::selectWorldCalibrationTarget(captureConfig,"pico","device","logical-pico","native",1);
    mb::TrajectoryRecorder capture(captureConfig,captureTarget,"controller-serial");
    for(std::size_t i=0;i<exact.size();++i){
        auto observation=sample("pico",static_cast<std::int64_t>(i));observation.timestamp_ns=static_cast<std::int64_t>(i)*20000000;observation.sent_at_ns=observation.timestamp_ns+2000000;
        observation.position=exact[i].source.pose.translation;observation.orientation=exact[i].source.pose.rotation;
        if(i%25==0){observation.modality="none";observation.position.reset();observation.orientation.reset();observation.validity={false,false};observation.orientation_evidence="none";observation.tracking_state="lost";}
        auto bytes=wire(observation);mb::c1::Envelope envelope;mb::c1::Error error;
        CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),envelope,error));
        capture.observation(std::get<mb::c1::TrackerObservation>(envelope),exact[i].source.timeNs+2000000);
        capture.reference(exact[i].reference,"controller-serial");
    }
    auto captured=capture.solve();close(captured.world,W,1e-7);close(captured.extrinsic,E,1e-7);CHECK(captured.pairs<exact.size()&&captured.quality.validRatio>.9);
    for(int i=1;i<=400;++i)capture.reference({exact.back().reference.timeNs+i*20000000LL,exact.back().reference.pose,true},"controller-serial");
    CHECK(capture.quality().validRatio<.7);reject([&]{capture.solve();}); // Source disappeared while reference kept running.
    auto changedHold=exact;mb::Rigid newE{mb::normalized({-.4,.2,.3,.7}),{-.2,.15,.3}};
    for(auto& pair:changedHold)pair.reference.pose=mb::composeRigid(mb::composeRigid(W,pair.source.pose),newE);
    auto newHold=mb::solveTrajectory(changedHold,1);close(newHold.world,W,1e-7);close(newHold.extrinsic,newE,1e-7);
    auto incompatible=captureConfig;auto sibling=captureTarget.binding;sibling.device="sibling";sibling.tracker="sibling";sibling.profile="another-profile";incompatible.bindings[{sibling.source,sibling.device}]=sibling;
    reject([&]{mb::validateTrajectoryTarget(incompatible,captureTarget,captureConfig.bridgeId);});
    std::cout<<"PASS trajectory: exact W/E, 6DoF, Gaussian noise, outliers, sign, lateral/yaw/static/planar rejection, time/rate/loss, identity/session/clock/revision, bounds, measure/apply\n";
    std::cout<<"noise rms m="<<noisy.residuals.rmsPosition<<" robust rms m="<<robust.residuals.rmsPosition<<" inliers="<<robust.inliers<<"/"<<robust.pairs<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

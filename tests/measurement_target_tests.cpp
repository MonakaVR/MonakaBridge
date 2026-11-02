#include "helpers.hpp"
#include "monaka_bridge/measurement_target.hpp"
#include "monaka_bridge/trajectory_calibration.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
const mb::Key selected{"pico","device"}, sibling{"pico","sibling"};
int cases=0,mutants=0;
void pass(const std::string& name) { ++cases;std::cout<<"PASS "<<name<<'\n'; }
template<class F> std::string rejected(F f) {
    try { f(); } catch(const std::invalid_argument& e) { return e.what(); }
    throw std::runtime_error("expected invalid_argument");
}
mb::Config fixture() {
    auto c=config();
    c.bindings.at(selected).spaceApproved=false;
    auto b=c.bindings.at(selected);b.device="sibling";b.tracker="logical-sibling";
    c.bindings.emplace(sibling,b);
    for(auto& [key,contract]:c.contracts) contract.inputDeclaration=contract.outputDeclaration={"position","orientation"};
    c.contracts.emplace(sibling,c.contracts.at(selected));
    mb::validateConfig(c);return c;
}
mb::FrozenMeasurementTarget freeze(const mb::Config& c) {
    return mb::freezeMeasurementTarget(c,"pico","device","logical-pico","native",1);
}
void mutation(const std::string& name,const std::function<void(mb::Config&)>& change,bool valid=true) {
    auto c=fixture();auto frozen=freeze(c);auto changed=c;change(changed);
    if(valid) mb::validateConfig(changed);
    else rejected([&]{mb::validateConfig(changed);});
    auto reason=rejected([&]{mb::validateFrozenMeasurementTarget(changed,frozen);});
    // A previous successful validation cannot cache the source or weaken a later check.
    mb::validateFrozenMeasurementTarget(c,frozen);
    mb::TrajectoryRecorder r(c,frozen,"synthetic-reference");r.checkConfig(c);
    rejected([&]{r.checkConfig(changed);});rejected([&]{r.checkConfig(c);});
    ++mutants;pass("mutation/"+name+"/"+(valid?"valid-config-scope-reject/":"invalid-config-reject/")+reason);
}
void positiveAndSeparation() {
    static_assert(!std::is_default_constructible_v<mb::FrozenMeasurementTarget>);
    static_assert(!std::is_copy_assignable_v<mb::FrozenMeasurementTarget>);
    static_assert(!std::is_move_assignable_v<mb::FrozenMeasurementTarget>);
    static_assert(std::is_copy_constructible_v<mb::FrozenMeasurementTarget>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<mb::FrozenMeasurementTarget&>().target())>>);
    auto c=fixture();const auto original=c;
    auto frozen=freeze(c);auto copy=frozen;
    CHECK(!c.bindings.at(selected).spaceApproved&&!frozen.target().binding.spaceApproved);
    CHECK(copy.publisher()==c.bridgeId&&copy.target().mappingRevision==c.revision);
    mb::TrajectoryRecorder recorder(c,copy,"synthetic-reference");recorder.checkConfig(c);
    auto p=sample();p.orientation=mb::Quat{0,0,0,1};
    CHECK(recorder.observation(p,1000000000));
    CHECK(recorder.sourceSamples().size()==1&&recorder.sourceSamples().back().valid);
    CHECK(recorder.sourceSamples().back().pose.translation==*p.position);
    recorder.reference({999000000,{},true},"synthetic-reference");CHECK(recorder.pairs().size()==1);
    CHECK(!c.bindings.at(selected).spaceApproved);pass("unapproved-space-typed-measurement-without-approval-copy");
    const auto legacy=frozen.target();
    mb::validateTrajectoryTarget(c,legacy,c.bridgeId);
    mb::TrajectoryRecorder legacyRecorder(c,legacy,"synthetic-reference");legacyRecorder.checkConfig(c);
    pass("legacy-validator-and-constructor-accept-defined-unapproved-space");
    mb::Bridge bridge(c,SB);CHECK(bridge.receive(wire(p),"synthetic-peer",10000000));
    CHECK(bridge.logical().at(selected).pose);
    bool normal=false;int poses=0;
    bridge.fanout.flush([&](mb::Channel channel,std::string_view bytes){
        if(channel==mb::Channel::Mirror)return true;
        mb::c1::Envelope e;mb::c1::Error error;
        CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),e,error));
        if(auto value=std::get_if<mb::c1::MtpPose>(&e)) { ++poses;CHECK(value->validity.position&&value->validity.orientation);CHECK(value->position==p.position); }
        if(auto value=std::get_if<mb::c1::MtpTrackerState>(&e)) if(value->tracker_id=="logical-pico") {
            CHECK(value->modality=="full"&&value->tracking_state=="tracked");
            CHECK(value->capabilities==c.contracts.at(selected).outputDeclaration);normal=true;
        }
        return true;
    },10000001);
    CHECK(normal&&poses>0&&!bridge.config().bindings.at(selected).spaceApproved);
    pass("in-memory-normal-output-uses-defined-unapproved-space-monaka-and-steamvr");
    c.bindings.at(selected).tracker="changed";c.profiles.at("pico").evidence="changed";
    c.contracts.at(selected).id="changed";c.bindings.erase(sibling);
    CHECK(frozen.target().binding.tracker=="logical-pico"&&frozen.target().profile.evidence==original.profiles.at("pico").evidence);
    mb::validateFrozenMeasurementTarget(original,frozen);mb::validateFrozenMeasurementTarget(original,copy);
    rejected([&]{mb::validateFrozenMeasurementTarget(c,frozen);});
    pass("deep-copy-frozen-after-original-binding-profile-contract-and-members-mutate");
    auto detached=[](){auto temporary=fixture();return freeze(temporary);}();
    mb::validateFrozenMeasurementTarget(original,detached);pass("snapshot-outlives-config");
    auto missing=fixture();missing.contracts.erase(sibling);auto absent=freeze(missing);
    mb::validateFrozenMeasurementTarget(missing,absent);
    missing.contracts.emplace(sibling,original.contracts.at(sibling));
    rejected([&]{mb::validateFrozenMeasurementTarget(missing,absent);});pass("missing-contract-frozen-without-capability-inference-addition-rejected");
    auto approved=config();auto approvedTarget=mb::selectWorldCalibrationTarget(approved,"pico","device","logical-pico","native",1);
    mb::TrajectoryRecorder oldApproved(approved,approvedTarget,"synthetic-reference");oldApproved.checkConfig(approved);
    pass("legacy-approved-entry-remains-usable");
    auto outside=original;outside.policy=mb::Policy::Disabled;outside.timeoutNs=1000000000;
    outside.sourceDocument="unknown-json-and-whitespace-are-B2";outside.sourcePath="synthetic-only";
    outside.bindings.at({"other","device"}).mount.translation[0]=1;
    mb::validateFrozenMeasurementTarget(outside,frozen);
    pass("explicit-non-guarantee-document-policy-timeout-and-outside-group-scope");
}
void groupAndFields() {
    mutation("publisher",[](auto& c){c.bridgeId="foreign-publisher";});
    mutation("revision",[](auto& c){++c.revision;});
    mutation("member-add",[](auto& c){auto b=c.bindings.at(sibling);b.device="new";b.tracker="new";c.bindings.emplace(mb::Key{b.source,b.device},b);});
    mutation("member-delete",[](auto& c){c.bindings.erase(sibling);c.contracts.erase(sibling);});
    for(auto key:{selected,sibling}) {
        auto prefix=key.second+"/";
        mutation(prefix+"key-move",[=](auto& c){auto b=c.bindings.at(key);c.bindings.erase(key);b.device+="-moved";c.bindings.emplace(mb::Key{b.source,b.device},b);});
        mutation(prefix+"source-and-key",[=](auto& c){auto b=c.bindings.at(key);c.bindings.erase(key);b.source="foreign";c.bindings.emplace(mb::Key{b.source,b.device},b);});
        for(auto field:{&mb::Binding::source,&mb::Binding::device})
            mutation(prefix+"invalid-binding-key",[=](auto& c){c.bindings.at(key).*field="foreign";},false);
        mutation(prefix+"tracker",[=](auto& c){c.bindings.at(key).tracker+="-changed";});
        mutation(prefix+"profile",[=](auto& c){c.profiles.emplace("another",c.profiles.at("pico"));c.bindings.at(key).profile="another";});
        mutation(prefix+"input-space",[=](auto& c){c.bindings.at(key).inputSpace="other-space";});
        mutation(prefix+"input-revision",[=](auto& c){++c.bindings.at(key).inputRevision;});
        mutation(prefix+"world-space",[=](auto& c){c.bindings.at(key).worldSpace="other-world";});
        mutation(prefix+"world-revision",[=](auto& c){++c.bindings.at(key).worldRevision;});
        mutation(prefix+"approval",[=](auto& c){c.bindings.at(key).spaceApproved=!c.bindings.at(key).spaceApproved;});
        for(bool mount:{false,true}) {
            auto rigid=mount?&mb::Binding::mount:&mb::Binding::world;
            auto name=prefix+(mount?"mount/":"world/");
            for(int i=0;i<3;++i) {
                mutation(name+"translation/"+std::to_string(i),[=](auto& c){(c.bindings.at(key).*rigid).translation[i]=std::nextafter(0.0,1.0);});
                mutation(name+"signed-zero/"+std::to_string(i),[=](auto& c){(c.bindings.at(key).*rigid).translation[i]=-0.0;});
            }
            for(int i=0;i<4;++i) {
                mutation(name+"rotation/"+std::to_string(i),[=](auto& c){auto& q=(c.bindings.at(key).*rigid).rotation;q[i]=std::nextafter(q[i],1.0-q[i]);});
                if(i<3) mutation(name+"rotation-signed-zero/"+std::to_string(i),[=](auto& c){(c.bindings.at(key).*rigid).rotation[i]=-0.0;});
            }
            mutation(name+"q-sign",[=](auto& c){for(auto& v:(c.bindings.at(key).*rigid).rotation)v=-v;});
            for(auto value:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
                mutation(name+"nonfinite-translation",[=](auto& c){(c.bindings.at(key).*rigid).translation[0]=value;},false);
                mutation(name+"nonfinite-rotation",[=](auto& c){(c.bindings.at(key).*rigid).rotation[0]=value;},false);
            }
        }
        mutation(prefix+"contract-remove",[=](auto& c){c.contracts.erase(key);});
        mutation(prefix+"contract-id",[=](auto& c){c.contracts.at(key).id+="-changed";});
        mutation(prefix+"contract-provenance",[=](auto& c){c.contracts.at(key).provenance+=" changed";});
        mutation(prefix+"input-policy",[=](auto& c){c.contracts.at(key).inputMode=monaka::bridge_input::Mode::STRICT;});
        mutation(prefix+"output-policy",[=](auto& c){c.contracts.at(key).outputMode=monaka::bridge_input::Mode::STRICT;});
        mutation(prefix+"input-declaration",[=](auto& c){c.contracts.at(key).inputDeclaration.push_back("battery_fraction");});
        mutation(prefix+"output-declaration",[=](auto& c){c.contracts.at(key).outputDeclaration.erase(c.contracts.at(key).outputDeclaration.begin());});
        mutation(prefix+"input-order",[=](auto& c){auto& v=c.contracts.at(key).inputDeclaration;std::swap(v[0],v[1]);});
        mutation(prefix+"output-order",[=](auto& c){auto& v=c.contracts.at(key).outputDeclaration;std::swap(v[0],v[1]);});
    }
    mutation("profile/convention",[](auto& c){c.profiles.at("pico").convention="other-native";});
    for(int i=0;i<3;++i) mutation("profile/position-axis/"+std::to_string(i),[=](auto& c){c.profiles.at("pico").positionAxes[i]*=-1;});
    for(int i=0;i<4;++i) mutation("profile/quaternion-axis/"+std::to_string(i),[=](auto& c){c.profiles.at("pico").quaternionAxes[i]*=-1;});
    mutation("profile/approved",[](auto& c){c.profiles.at("pico").approved=false;});
    mutation("profile/evidence",[](auto& c){c.profiles.at("pico").evidence+=" changed";});
    mutation("profile/angular",[](auto& c){c.profiles.at("pico").angularSpaceVerified=true;});
    mutation("profile/missing",[](auto& c){c.profiles.erase("pico");});
    mutation("profile/empty-evidence",[](auto& c){c.profiles.at("pico").evidence.clear();});
    mutation("profile/invalid-axis",[](auto& c){c.profiles.at("pico").positionAxes[0]=0;},false);
}
mb::Quat axis(int i,double a) { mb::Quat q{0,0,0,std::cos(a/2)};q[i]=std::sin(a/2);return q; }
void completeUnapprovedMeasurement() {
    auto c=fixture();c.profiles.at("pico").quaternionAxes={1,2,3,4};
    c.bindings.at(selected).mount={axis(2,.5),{1,2,3}};
    const mb::Rigid world{mb::normalized({.2,-.3,.1,.9}),{1,2,-.5}};
    const mb::Rigid extrinsic{mb::normalized({.3,.1,-.2,.9}),{.1,.05,-.2}};
    auto frozen=freeze(c);mb::TrajectoryRecorder r(c,frozen,"synthetic-reference");
    for(int i=0;i<=500;++i) {
        double t=i*.02;
        mb::Rigid source{mb::mul(mb::mul(axis(0,.8*std::sin(1.3*t)),axis(1,.9*std::sin(.9*t))),axis(2,.7*std::sin(1.7*t))),
            {.45*std::sin(t),.35*std::sin(1.4*t),.30*std::sin(.7*t)}};
        auto reference=mb::composeRigid(mb::composeRigid(world,source),extrinsic);
        auto p=sample("pico",i);p.timestamp_ns=i*20000000LL;p.sent_at_ns=p.timestamp_ns+2000000;
        p.position=source.translation;p.orientation=source.rotation;
        mb::c1::Envelope e;mb::c1::Error error;auto bytes=wire(p);
        CHECK(mb::c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),e,error));
        auto time=1000000000LL+p.timestamp_ns;
        CHECK(r.observation(std::get<mb::c1::TrackerObservation>(e),time+2000000));
        r.reference({time,reference,true},"synthetic-reference");
    }
    r.checkConfig(c);const auto result=r.solve();
    CHECK(result.pairs==501&&result.inliers==501&&result.quality.sufficient);
    CHECK(mb::norm(mb::add(result.world.translation,mb::scale(world.translation,-1)))<1e-7);
    CHECK(mb::rotationDistance(result.world.rotation,world.rotation)<1e-7);
    CHECK(mb::norm(mb::add(result.extrinsic.translation,mb::scale(extrinsic.translation,-1)))<1e-7);
    CHECK(mb::rotationDistance(result.extrinsic.rotation,extrinsic.rotation)<1e-7);
    CHECK(!c.bindings.at(selected).spaceApproved&&!frozen.target().binding.spaceApproved);
    CHECK(c.bindings.at(selected).mount.translation==mb::Vec({1,2,3}));
    std::cout<<"MEASURE_ONLY pairs="<<result.pairs<<" inliers="<<result.inliers
        <<" rms_position="<<result.residuals.rmsPosition<<" rms_orientation="<<result.residuals.rmsOrientation<<'\n';
    pass("codec-to-new-typed-recorder-501-pair-W-E-solve-unapproved-space-nonzero-mount-unchanged");
}
void factoryGuards() {
    auto c=fixture();
    for(int mode=0;mode<5;++mode) {
        rejected([&]{mb::freezeMeasurementTarget(c,mode==0?"foreign":"pico",mode==1?"foreign":"device",
            mode==2?"foreign":"logical-pico",mode==3?"foreign":"native",mode==4?2:1);});
        pass("factory-exact-selection/"+std::to_string(mode));
    }
    for(int mode=0;mode<6;++mode) {
        auto bad=c;
        if(mode==0)bad.profiles.erase("pico");if(mode==1)bad.profiles.at("pico").approved=false;
        if(mode==2)bad.profiles.at("pico").evidence.clear();
        if(mode==3)bad.bindings.at(sibling).profile="unknown";
        if(mode==4)bad.bindings.at(sibling).worldSpace="incompatible";
        if(mode==5)++bad.bindings.at(sibling).worldRevision;
        if(mode==1||mode==2) { auto ordinary=freeze(bad);mb::validateFrozenMeasurementTarget(bad,ordinary);pass("factory-quality-metadata-optional/"+std::to_string(mode)); }
        else { rejected([&]{freeze(bad);});pass("factory-profile-group-constraint/"+std::to_string(mode)); }
    }
    auto frozen=freeze(c);
    rejected([&]{mb::TrajectoryRecorder r(c,frozen,"");});
    auto o=mb::TrajectoryOptions{};o.maxSamples=0;
    rejected([&]{mb::TrajectoryRecorder r(c,frozen,"synthetic-reference",o);});
    pass("new-entry-retains-reference-and-options-validation");
}
} // namespace
int main()try {
    positiveAndSeparation();groupAndFields();completeUnapprovedMeasurement();factoryGuards();
    std::cout<<"PASS measurement_target: "<<cases<<" cases; "<<mutants<<" typed configuration drift mutants rejected\n";
    std::cout<<"NOT_GUARANTEED B2-B4: operator confirmation; pre-confirm session/clock; initial sequence floors; live freshness/peer attribution; document CAS; GUI/OpenVR/IPC/static workflow/packaging; physical/HIL acceptance\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL measurement_target after "<<cases<<" cases: "<<e.what()<<'\n';return 1;}

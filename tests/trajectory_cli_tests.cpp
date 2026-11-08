// Compile the real CLI parsing and OpenVR matrix conversion without initializing
// SteamVR. This executable never calls VR_Init or starts a service.
#include "helpers.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include <fstream>
#define main calibrator_entry_not_called
#include "../tools/steamvr_calibrator.cpp"
#undef main

int main(int argc,char** argv)try{
    auto parse=[](std::vector<std::string> args,Options& o){std::vector<char*> values;for(auto& s:args)values.push_back(s.data());return ParseArgs(static_cast<int>(values.size()),values.data(),o);};
    std::vector<std::string> base{"calibrator","--config","unused.json","--tracker","tracker","--source","source","--device","device","--input-space","native","--input-revision","1","--reference","left"};
    Options legacy;CHECK(parse(base,legacy)&&legacy.mode=="static"&&!legacy.apply);
    for(auto mode:{"static","trajectory"}){auto args=base;args.insert(args.end(),{"--mode",mode,"--measure-only"});Options o;CHECK(parse(args,o)&&!o.apply&&o.mode==mode);}
    for(auto seconds:{"4","16","NaN"}){auto args=base;args.insert(args.end(),{"--mode","trajectory","--duration-seconds",seconds});Options o;CHECK(!parse(args,o));}
    auto args=base;args.insert(args.end(),{"--mode","trajectory","--duration-seconds","5","--max-pairing-ms","10","--apply"});Options apply;CHECK(parse(args,apply)&&apply.apply&&apply.trajectory.durationSeconds==5&&apply.trajectory.maxPairingDeltaNs==10000000);
    args.push_back("--measure-only");Options conflict;CHECK(!parse(args,conflict));
    for(auto rotation:{mb::Quat{0,0,0,1},mb::normalized({.2,-.4,.7,.5}),mb::Quat{1,0,0,0}}){
        vr::TrackedDevicePose_t p{};mb::Vec translation{1.2,-.5,2.1};
        for(int j=0;j<3;++j){mb::Vec unit{};unit[j]=1;auto column=mb::rotate(rotation,unit);for(int i=0;i<3;++i)p.mDeviceToAbsoluteTracking.m[i][j]=static_cast<float>(column[i]);p.mDeviceToAbsoluteTracking.m[j][3]=static_cast<float>(translation[j]);}
        auto converted=ReferenceRigid(p);CHECK(mb::rotationDistance(converted.rotation,rotation)<1e-6);CHECK(mb::norm(mb::add(converted.translation,mb::scale(translation,-1)))<1e-6);
        for(int i=0;i<3;++i)p.mDeviceToAbsoluteTracking.m[i][0]*=2;
        bool rejected=false;try{ReferenceRigid(p);}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);
    }
    // Both legacy capture modes call this same private poll boundary. Exercise
    // its actual API arguments and full matrix result without invoking an SDK.
    struct ReferenceApi {
        mb::Rigid raw{mb::normalized({.2,.1,-.3,.9}),{.4,-.8,1.2}};
        mb::Rigid room{mb::normalized({-.1,.5,.2,.8}),{2.1,.7,-1.3}};
        vr::ETrackingUniverseOrigin seen=vr::TrackingUniverseSeated;int calls=0;
        void GetDeviceToAbsoluteTrackingPose(vr::ETrackingUniverseOrigin origin,float prediction,vr::TrackedDevicePose_t* poses,std::uint32_t count){
            CHECK(prediction==0&&count==vr::k_unMaxTrackedDeviceCount);seen=origin;++calls;
            auto pose=origin==vr::TrackingUniverseStanding?mb::composeRigid(room,raw):raw;
            CHECK(origin==vr::TrackingUniverseStanding||origin==vr::TrackingUniverseRawAndUncalibrated);
            auto& out=poses[1];out.bDeviceIsConnected=out.bPoseIsValid=true;
            for(int column=0;column<3;++column){mb::Vec axis{};axis[column]=1;auto v=mb::rotate(pose.rotation,axis);for(int row=0;row<3;++row)out.mDeviceToAbsoluteTracking.m[row][column]=float(v[row]);}
            for(int row=0;row<3;++row)out.mDeviceToAbsoluteTracking.m[row][3]=float(pose.translation[row]);
        }
    } reference;
    CHECK(argc>=1);const auto fixtureRoot=std::filesystem::absolute(std::filesystem::u8path(argv[0])).parent_path()/("reference-origin-"+mb::local_calibration::opaqueId());std::filesystem::create_directories(fixtureRoot);
    for(auto policy:{mb::Policy::Steamvr,mb::Policy::Monaka,mb::Policy::Both,mb::Policy::Disabled}){
        AlignmentMapping mapping;mapping.config=config();mapping.config.policy=policy;
        mapping.path=(fixtureRoot/(mb::policyName(policy)+".json")).u8string();mb::saveConfig(mapping.path,mapping.config);
        mapping.target=mb::selectWorldCalibrationTarget(mapping.config,"pico","device","logical-pico","native",1);
        for(const char* mode:{"static","trajectory"}){
            (void)mode;vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};PollReferencePoses(&reference,mapping,poses);
            const auto expected=policy==mb::Policy::Steamvr?reference.raw:mb::composeRigid(reference.room,reference.raw);auto actual=ReferenceRigid(poses[1]);
            CHECK(reference.seen==(policy==mb::Policy::Steamvr?vr::TrackingUniverseRawAndUncalibrated:vr::TrackingUniverseStanding));
            CHECK(mb::rotationDistance(actual.rotation,expected.rotation)<1e-6&&mb::norm(mb::add(actual.translation,mb::scale(expected.translation,-1)))<1e-6);
        }
        std::string error;CHECK(CheckMapping(mapping,error));auto changed=mb::local_calibration::Json::parse(mb::readConfigBytes(mapping.path));changed["policy"]=mb::policyName(policy==mb::Policy::Steamvr?mb::Policy::Monaka:mb::Policy::Steamvr);
        {std::ofstream file(mapping.path,std::ios::binary|std::ios::trunc);file<<changed.dump();CHECK(bool(file));}const auto external=mb::readConfigBytes(mapping.path);
        CHECK(!CheckMapping(mapping,error)&&!WriteAlignment(mapping,{},error)&&mb::readConfigBytes(mapping.path)==external);
    }
    CHECK(reference.calls==8);
    std::cout<<"PASS legacy static/trajectory shared API boundary: Steamvr Raw, other policies Standing, nonidentity room/full pose and same-revision policy drift refusal\n";
    std::cout<<"PASS calibrator CLI: static compatibility, trajectory explicit apply/bounds, actual OpenVR matrix conversion; runtime NOT RUN\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

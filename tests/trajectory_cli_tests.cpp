// Compile the real CLI parsing and OpenVR matrix conversion without initializing
// SteamVR. This executable never calls VR_Init or starts a service.
#include "helpers.hpp"
#define main calibrator_entry_not_called
#include "../tools/steamvr_calibrator.cpp"
#undef main

int main()try{
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
    std::cout<<"PASS calibrator CLI: static compatibility, trajectory explicit apply/bounds, actual OpenVR matrix conversion; runtime NOT RUN\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

// Prepare a separate explicit presentation snapshot; no driver registration or config mutation.
#include "monaka_bridge/direct.hpp"
#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include <fstream>
#include <iostream>
using namespace mb;
using namespace mb::local_calibration;
int main(int argc,char** argv)try{
 if(argc!=6&&argc!=7)throw std::runtime_error("usage: prepare_presentation config.json health.json source device new-output-directory [model-id]");
 const std::string model=argc==7?argv[6]:"pico_motion_tracker";
 if(model!="pico_motion_tracker"&&model!="pico_tracker_rev3_identification"&&model!="pico_tracker_rev3_axis_debug")throw std::runtime_error("UNSUPPORTED_MODEL");
 const auto bytes=readConfigBytes(argv[1]);auto config=parseConfigDocument(bytes);auto health=Json::parse(readConfigBytes(argv[2]));auto out=std::filesystem::absolute(argv[5]);
 auto require=[](bool okay,const char* why){if(!okay)throw std::runtime_error(why);};
 require(!std::filesystem::exists(out),"OUTPUT_MUST_BE_NEW");
 const auto utc=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 const auto stamp=health.at("snapshot_unix_ms").get<std::int64_t>();require(stamp<=utc&&utc-stamp<=2000,"HEALTH_SNAPSHOT_STALE");
 require(health.at("publisher_id")==config.bridgeId&&health.at("mapping_revision")==config.revision,"HEALTH_CONFIG_MISMATCH");
 const auto& b=config.bindings.at({argv[3],argv[4]});const auto& p=config.profiles.at(b.profile);
 require(b.spaceApproved&&p.approved&&!p.evidence.empty()&&config.contracts.count({b.source,b.device}),"OUTPUT_APPROVAL_REQUIRED");
 require(config.policy==Policy::Steamvr||config.policy==Policy::Both,"DIRECT_ROUTE_REQUIRED");
 Json observed;unsigned count=0;for(const auto& row:health.at("devices"))if(row.at("source")==b.source&&row.at("device")==b.device){observed=row;++count;}
 require(count==1&&observed.at("fresh")==true&&observed.at("position_valid")==true&&observed.at("orientation_valid")==true&&!observed.at("collision").get<bool>(),"FRESH_EXACT_PQ_REQUIRED");
 require(observed.at("space")==b.inputSpace&&observed.at("revision")==b.inputRevision&&observed.at("convention")==p.convention,"SOURCE_SPACE_MISMATCH");
 const auto serial=runtimeSerial(outputSource(config.bridgeId,b.source),b.tracker);
 Json selection={{"namespace","monaka.bridge.presentation"},{"version",1},{"publisher_session",health.at("publisher_session")},{"config_file","bridge-config.json"},{"config_sha256",sha256(bytes)},
  {"assignments",Json::array({{{"model_id",model},{"device_kind","pico_motion_tracker"},{"source_id",b.source},{"device_id",b.device},{"tracker_id",b.tracker},{"runtime_serial",serial},{"input_session",observed.at("input_session")}}})}};
 std::filesystem::create_directories(out);
 {std::ofstream f(out/"bridge-config.json",std::ios::binary);f<<bytes;f.close();require(bool(f),"SNAPSHOT_WRITE_FAILED");}
 {std::ofstream f(out/"selection.json",std::ios::binary);f<<selection.dump(2)<<'\n';f.close();require(bool(f),"SELECTION_WRITE_FAILED");}
 std::cout<<Json({{"status","PREPARED_NOT_APPLIED"},{"output",out.u8string()},{"runtime_serial",serial},{"config_sha256",sha256(bytes)},{"registration_changed",false}}).dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include "monaka_bridge/trajectory_result_json.hpp"
#include <fstream>
#include <iostream>
using namespace mb;
using namespace mb::local_calibration;
int main(int argc,char** argv) {
 try {
  if(argc!=4)throw std::runtime_error("usage: trajectory_replay raw.json result.json output.json");
  const auto rawBytes=readConfigBytes(argv[1]),oldBytes=readConfigBytes(argv[2]);
  auto raw=Json::parse(rawBytes),old=Json::parse(oldBytes);
  std::vector<TimedCalibrationPose> source,reference;
  auto read=[](const Json& input,std::vector<TimedCalibrationPose>& out) {
   for(const auto& s:input){TimedCalibrationPose p;
    p.timeNs=s.at("time_ns").get<std::int64_t>();p.valid=s.at("valid").get<bool>();
    p.pose.rotation=s.at("pose").at("rotation_xyzw").get<Quat>();p.pose.translation=s.at("pose").at("translation_m").get<Vec>();
    if(p.timeNs<0||(!out.empty()&&p.timeNs<=out.back().timeNs))throw std::runtime_error("REPLAY_TIME_ORDER");
    for(double v:p.pose.rotation)if(!std::isfinite(v))throw std::runtime_error("REPLAY_NONFINITE");
    for(double v:p.pose.translation)if(!std::isfinite(v))throw std::runtime_error("REPLAY_NONFINITE");
    out.push_back(p);
   }
   if(out.empty()||out.size()>4096)throw std::runtime_error("REPLAY_SAMPLE_LIMIT");
  };
  read(raw.at("source"),source);read(raw.at("reference"),reference);
  TrajectoryOptions options;auto pairs=pairTrajectory(source,reference,options);
  double ratio=std::min(double(pairs.size())/source.size(),double(pairTrajectory(reference,source,options).size())/reference.size());
  const auto result=solveTrajectory(pairs,ratio,options);auto out=trajectoryResultJson(result);
  for(const auto* key:{"world","extrinsic_not_mount","pairs","inliers","rms_position","rms_orientation"})
   if(out.at(key)!=old.at(key))throw std::runtime_error(std::string("REPLAY_RESULT_MISMATCH:")+key);
  out.update({{"result_id",old.at("result_id")},{"config_sha256",old.at("config_sha256")},{"mount_existing",old.at("mount_existing")},
   {"raw_sha256",sha256(rawBytes)},{"original_result_sha256",sha256(oldBytes)},{"evidence_kind","OFFLINE_NATIVE_REPLAY_OF_RECORDED_CAPTURE"}});
  std::ofstream f(argv[3],std::ios::binary);f<<out.dump(2)<<'\n';f.close();if(!f)throw std::runtime_error("REPLAY_WRITE_FAILED");
  std::cout<<out.dump()<<'\n';return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

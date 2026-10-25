#include "monaka_bridge/static_calibration_session.hpp"
#include <algorithm>
#include <limits>

namespace mb::local_calibration {
namespace {void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}}
void StaticOptions::validate()const{
 need(points>=3&&points<=16&&samples>=1&&samples<=4096&&points*samples<=16384,"STATIC_PLAN_LIMIT");
}
std::int64_t StaticOptions::pointTimeout()const{return std::max<std::int64_t>(5*second,std::min<std::int64_t>(60*second,samples*8*30*1000000LL));}
StaticMeasurement::StaticMeasurement(const FrozenMeasurementTarget& target,std::string session,StaticOptions options)
 :options_(options),target_(target.target()),session_(std::move(session)){options_.validate();}
void StaticMeasurement::beginPoint(std::int64_t start){
 need(points_.size()<options_.points,"STATIC_POINT_LIMIT");start_=start;
 source_.clear();reference_.clear();usedReference_.clear();cursor_=pairs_=0;sourceSum_={};referenceSum_={};
}
bool StaticMeasurement::consume(const RecordingDelivery& delivery){
 need(delivery.values.frames.size()==delivery.eligible.size()&&delivery.values.references.size()==delivery.referenceEligible.size(),"STATIC_DELIVERY_SHAPE");
 for(std::size_t i=0;i<delivery.values.references.size();++i){
  const auto& r=delivery.values.references[i];if(r.timeNs<start_||r.timeNs<=lastReference_)continue;
  need(++totalReference_<=16384,"STATIC_REFERENCE_SAMPLE_LIMIT");lastReference_=r.timeNs;
  auto value=r;value.valid=delivery.referenceEligible[i];reference_.push_back(value);
 }
 for(std::size_t i=0;i<delivery.values.frames.size();++i){
  const auto& f=delivery.values.frames[i];auto p=std::get_if<c1::TrackerObservation>(&f.envelope);if(!p)continue;
  const auto age=p->sent_at_ns-p->timestamp_ns;need(age>=0&&f.receiptNs>=age,"STATIC_SOURCE_TIME_INVALID");
  const auto t=f.receiptNs-age;if(t<start_||t<=lastSource_)continue;
  need(++totalSource_<=16384,"STATIC_SOURCE_SAMPLE_LIMIT");lastSource_=t;
  TimedCalibrationPose value;value.timeNs=t;value.valid=delivery.eligible[i];
  if(value.valid)value.pose.translation=calibrationSourcePoint(*p,target_,session_);source_.push_back(value);
 }
 pair();return pairs_>=options_.samples;
}
void StaticMeasurement::pair(){
 while(cursor_<source_.size()&&pairs_<options_.samples){
  const auto& s=source_[cursor_];
  // Wait for a reference at/after source time; do not prematurely choose latest.
  auto after=std::lower_bound(reference_.begin(),reference_.end(),s.timeNs,[](const auto& r,auto t){return r.timeNs<t;});
  if(after==reference_.end())break;
  auto best=after;if(after!=reference_.begin()){auto before=std::prev(after);if(s.timeNs-before->timeNs<=after->timeNs-s.timeNs)best=before;}
  ++cursor_;
  // Nearest sample includes invalid entries. Never jump across an invalid gap.
  if(!s.valid||!best->valid||std::llabs(best->timeNs-s.timeNs)>20000000||usedReference_.count(best->timeNs))continue;
  usedReference_.insert(best->timeNs);sourceSum_=add(sourceSum_,s.pose.translation);referenceSum_=add(referenceSum_,best->pose.translation);++pairs_;
 }
}
void StaticMeasurement::finishPoint(){
 need(pairs_==options_.samples,"STATIC_POINT_INCOMPLETE");
 auto point=PointCorrespondence{scale(sourceSum_,1.0/pairs_),scale(referenceSum_,1.0/pairs_)};points_.push_back(point);
 Json samples={{"source",Json::array()},{"reference",Json::array()},{"pairs",pairs_},{"start_ns",start_}};
 for(const auto& s:source_)samples["source"].push_back({{"time_ns",s.timeNs},{"valid",s.valid},{"position",s.pose.translation}});
 for(const auto& s:reference_)samples["reference"].push_back({{"time_ns",s.timeNs},{"valid",s.valid},{"position",s.pose.translation}});
 samples["source_mean"]=point.source;samples["reference_mean"]=point.reference;raw_.push_back(std::move(samples));
}
RigidAlignmentResult StaticMeasurement::solve()const{need(points_.size()==options_.points,"STATIC_POINTS_INCOMPLETE");return solveRigidAlignment(points_);}
} // namespace mb::local_calibration

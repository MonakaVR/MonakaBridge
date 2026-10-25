#pragma once
#include "calibration_session.hpp"
#include <condition_variable>
#include <thread>

namespace mb::local_calibration {
// Internal state, not another observation/wire model. The owner consumes each
// canonical input once; the worker only records already classified values.
struct RecordingDelivery {
 ProviderBatch values;
 std::vector<bool> eligible,referenceEligible;
 std::uint64_t received=0,delivery=0;
};
struct CaptureSnapshot {
 std::int64_t poseHigh=-1,lastValidPose=-1,referenceAt=-1,lastValidReference=-1;
 std::optional<std::int64_t> stateHigh;
 bool poseValid=false,referenceValid=false;
 std::string fault,shutdownFault;
 std::uint64_t invalidationEpoch=0,received=0,deliveryIssued=0,deliveryAcked=0,stopReceived=0,stopDelivery=0;
 std::size_t framesOutstanding=0,referencesOutstanding=0;
 bool captureDone=false;
};
class CaptureMonitor {
public:
 CaptureMonitor(const ProviderOpenOptions&,const ProviderBatch&,std::int64_t now,std::int64_t created);
 RecordingDelivery consume(const ProviderBatch&,bool recording,std::int64_t now);
 void tick(std::int64_t now);
 void fail(const std::string&);
 const InputWitness& witness()const{return input_;}
 CaptureSnapshot snapshot()const{return state_;}
 void requireLive(std::int64_t now,bool fresh);
private:
 struct Counter {
  std::optional<std::int64_t> high;
  std::deque<std::pair<std::int64_t,std::string>> recent;
  bool accept(std::int64_t,const std::string&);
 } poses_,states_;
 InputWitness input_;std::vector<std::string> declaration_;
  bool orientationRequired_=true;
 CaptureSnapshot state_;std::int64_t created_,lastNow_,lastPoseReceipt_=-1,lastStateReceipt_=-1;
 // Only owner samples advance lastNow_; worker freshness reads can overtake a
 // sample captured before the mutex. They must not move its clock watermark.
 void checkLifetime(std::int64_t now);
 void check(bool,const char*);
};

// Lazy, one input owner. No filesystem, Session callback, SDK or socket here.
// N1 may implement the existing CaptureProvider using its own SDK/transport.
class CaptureOwner {
 struct Context;
 std::shared_ptr<Context> context_;
 std::thread thread_;
public:
 struct Barrier {
  std::string run,request;
  std::uint64_t generation=0,id=0,epoch=0,received=0;
 };
 class Admission {
  std::unique_lock<std::mutex> lock_;
  explicit Admission(std::unique_lock<std::mutex> lock):lock_(std::move(lock)){}
  friend class CaptureOwner;
 public: Admission(Admission&&)=default;
 };
 CaptureOwner(CaptureProvider&,Clock,const std::atomic<bool>& cancel,std::int64_t created,std::string run);
 ~CaptureOwner();
 InputWitness discover();
 CaptureSnapshot snapshot()const;
 CaptureSnapshot check(std::int64_t now,bool fresh);
 Barrier barrier(const std::string& request,std::int64_t maximumWait=10*second);
 Admission admit(const Barrier&,std::int64_t now);
 void record(const std::function<void(const RecordingDelivery&)>&);
  void recordPoint(const std::function<bool(const RecordingDelivery&)>&,std::int64_t deadline);
 void invalidate(const std::string&);
 void shutdown();
 CaptureOwner(const CaptureOwner&)=delete;
};
} // namespace mb::local_calibration

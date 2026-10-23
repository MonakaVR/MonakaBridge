#pragma once
#include "calibration_capture_monitor.hpp"
#include <set>

namespace mb::local_calibration {
// R2 software timing policy. None of these bounds declares physical accuracy.
struct StaticOptions {
 std::size_t points=3,samples=60;
 void validate()const;
 std::int64_t pointTimeout()const;
};
class StaticMeasurement {
 StaticOptions options_;WorldCalibrationTarget target_;
 std::optional<std::string> session_;
 std::vector<TimedCalibrationPose> source_,reference_;
 std::vector<PointCorrespondence> points_;
 std::set<std::int64_t> usedReference_;
 std::size_t cursor_=0,totalSource_=0,totalReference_=0,pairs_=0;
 Vec sourceSum_{},referenceSum_{};
 std::int64_t start_=0,lastSource_=-1,lastReference_=-1;
 Json raw_=Json::array();
 void pair();
public:
 StaticMeasurement(const FrozenMeasurementTarget&,std::string,StaticOptions);
 void beginPoint(std::int64_t);
 bool consume(const RecordingDelivery&);
 void finishPoint();
 std::size_t pointCount()const{return points_.size();}
 std::size_t pairCount()const{return pairs_;}
 const StaticOptions& options()const{return options_;}
 RigidAlignmentResult solve()const;
 const Json& raw()const{return raw_;}
};

// Native authority interface. Production verifies an external hash-pinned policy
// and evidence set. Synthetic implementations remain test-only.
struct OutputApprovalCriteria {
 std::string policyId,evidenceId,evidenceSha256;
 std::int64_t expiresAt=0;
 std::set<Key> eligible;
};
struct OutputApprovalContext {
 const FrozenMeasurementTarget& target;
 const InputWitness& input;
 const Config& config;
 const std::string& configSha256;
 std::int64_t now;
};
class OutputApprovalAuthority {
public:
 virtual ~OutputApprovalAuthority()=default;
 virtual OutputApprovalCriteria verify(const OutputApprovalContext&)const=0;
};
} // namespace mb::local_calibration

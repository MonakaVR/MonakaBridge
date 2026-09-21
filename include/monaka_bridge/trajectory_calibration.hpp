#pragma once
#include "world_calibration.hpp"

namespace mb {

// Software defaults, not hardware accuracy claims. Distances are metres, angles
// radians, times local monotonic nanoseconds. No wire/config schema is extended.
struct TrajectoryOptions {
    double durationSeconds=10, minSpanSeconds=4.5, minValidRatio=.7;
    Vec minExtent{.15,.15,.10};
    double minRotation=2.0, minAxisDiversity=.01;
    std::int64_t maxPairingDeltaNs=20000000, maxAgeNs=50000000;
    std::int64_t qualityRotationStepNs=100000000;
    std::size_t minSamples=80, maxSamples=4096, maxSolveSamples=240, maxMotions=64;
    double minMotionAngle=.10, maxMotionAngle=2.8, minTranslationPivotRatio=.001;
    double motionTrimFloor=.04, motionTrimMultiplier=3;
    double inlierPosition=.05, inlierOrientation=.15, minInlierRatio=.8;
    double maxRmsPosition=.025, maxRmsOrientation=.08;
};
struct TimedCalibrationPose {
    std::int64_t timeNs=0;
    Rigid pose;
    bool valid=false;
};
struct TrajectoryPair {
    TimedCalibrationPose source, reference;
};
struct TrajectoryQuality {
    Vec extent{};
    double rotation=0, axisDiversity=0, validRatio=0, spanSeconds=0;
    std::size_t samples=0;
    bool sufficient=false;
    std::string reason;
};
struct TrajectoryResiduals {
    double rmsPosition=0, maxPosition=0, rmsOrientation=0, maxOrientation=0;
};
struct TrajectoryResult {
    Rigid world, extrinsic;
    TrajectoryQuality quality;
    TrajectoryResiduals residuals, allResiduals;
    std::size_t inliers=0, pairs=0;
};

Rigid composeRigid(const Rigid& a,const Rigid& b);
Rigid inverseRigid(const Rigid& value);
double rotationDistance(Quat a,Quat b);
void validateTrajectoryOptions(const TrajectoryOptions& options);
std::vector<TrajectoryPair> pairTrajectory(const std::vector<TimedCalibrationPose>& source,
    const std::vector<TimedCalibrationPose>& reference,const TrajectoryOptions& options);
TrajectoryQuality trajectoryQuality(const std::vector<TrajectoryPair>& pairs,
    double validRatio,const TrajectoryOptions& options);
// Also usable as the objective evaluation boundary for future joint refinement.
TrajectoryResiduals trajectoryResiduals(const std::vector<TrajectoryPair>& pairs,
    const Rigid& world,const Rigid& extrinsic);
TrajectoryResult solveTrajectory(const std::vector<TrajectoryPair>& pairs,
    double validRatio,const TrajectoryOptions& options={});

// Single owner (calibrator thread), bounded recording. Never updates Bridge state.
// Stores a frozen identity/profile/config target separately from timestamped poses.
class TrajectoryRecorder {
public:
    TrajectoryRecorder(const Config&,const WorldCalibrationTarget&,std::string referenceId,
        TrajectoryOptions options={});
    void checkConfig(const Config&);
    bool observation(const c1::TrackerObservation&,std::int64_t arrivalNs);
    void deviceState(const c1::ObservationDeviceState&);
    void reference(TimedCalibrationPose,std::string referenceId);
    std::vector<TrajectoryPair> pairs() const;
    TrajectoryQuality quality() const;
    TrajectoryResult solve() const;
    const std::optional<std::string>& session() const {return session_;}
    const std::vector<TimedCalibrationPose>& sourceSamples() const {return source_;}
    const std::vector<TimedCalibrationPose>& referenceSamples() const {return reference_;}
private:
    void require(bool,const char*);
    double ratio(std::size_t paired) const;
    WorldCalibrationTarget target_;
    std::string publisher_, referenceId_, clock_, failure_;
    TrajectoryOptions options_;
    std::optional<std::string> session_;
    std::int64_t sequence_=-1, lastArrival_=-1;
    std::size_t attempts_=0;
    std::vector<TimedCalibrationPose> source_,reference_;
};
// Extra source/profile group and publisher guards, while reusing static apply.
void validateTrajectoryTarget(const Config&,const WorldCalibrationTarget&,const std::string& publisher);
} // namespace mb

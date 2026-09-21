#include "monaka_bridge/trajectory_calibration.hpp"
#include <algorithm>
#include <limits>
#include <numeric>

namespace mb {
namespace {
using Matrix=std::array<Vec,3>;
Vec sub(Vec a,Vec b){return add(a,scale(b,-1));}
double dot(Vec a,Vec b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
void ensure(bool ok,const char* why){if(!ok)throw std::invalid_argument(why);}
bool finite(const Rigid& p){for(auto x:p.translation)if(!std::isfinite(x))return false;for(auto x:p.rotation)if(!std::isfinite(x))return false;double n=0;for(auto x:p.rotation)n+=x*x;return std::abs(n-1)<1e-4;}
Vec logRotation(Quat q){q=normalized(q);if(q[3]<0)for(auto& x:q)x=-x;Vec v{q[0],q[1],q[2]};double s=norm(v);return s<1e-12?Vec{}:scale(v,2*std::atan2(s,q[3])/s);}
double quantile(std::vector<double> values,double fraction){ensure(!values.empty(),"empty trajectory statistic");std::sort(values.begin(),values.end());return values[static_cast<std::size_t>((values.size()-1)*fraction)];}
double determinant(const Matrix& m){return dot(m[0],cross(m[1],m[2]));}
void outer(Matrix& m,Vec a,Vec b){for(int i=0;i<3;++i)for(int j=0;j<3;++j)m[i][j]+=a[i]*b[j];}
Vec linearSolve(Matrix m,Vec b,double ratio){
    double maximum=0;for(int i=0;i<3;++i)maximum=std::max(maximum,std::abs(m[i][i]));
    ensure(maximum>1e-12,"degenerate hand-eye translation");
    for(int col=0;col<3;++col){
        int pivot=col;for(int row=col+1;row<3;++row)if(std::abs(m[row][col])>std::abs(m[pivot][col]))pivot=row;
        ensure(std::abs(m[pivot][col])>maximum*ratio,"ill-conditioned hand-eye translation");
        std::swap(m[pivot],m[col]);std::swap(b[pivot],b[col]);
        double v=m[col][col];for(int j=col;j<3;++j)m[col][j]/=v;b[col]/=v;
        for(int row=0;row<3;++row)if(row!=col){double f=m[row][col];for(int j=col;j<3;++j)m[row][j]-=f*m[col][j];b[row]-=f*b[col];}
    }return b;
}
struct Motion{Rigid a,b;};
std::vector<Motion> motions(const std::vector<TrajectoryPair>& pairs,const TrajectoryOptions& o){
    std::vector<Motion> result;
    // Deterministic stratified relative motions, bounded independently of rate.
    for(std::size_t k=0;k<o.maxMotions*8&&result.size()<o.maxMotions;++k){
        std::size_t i=(k%o.maxMotions)*(pairs.size()-1)/o.maxMotions,gap=std::max<std::size_t>(1,pairs.size()*(1+k%4)/5),j=(i+gap)%pairs.size();
        if(i>j)std::swap(i,j);if(i==j)continue;
        auto a=composeRigid(inverseRigid(pairs[i].source.pose),pairs[j].source.pose);
        auto b=composeRigid(inverseRigid(pairs[i].reference.pose),pairs[j].reference.pose);
        double angleA=norm(logRotation(a.rotation)),angleB=norm(logRotation(b.rotation));
        if(angleA>=o.minMotionAngle&&angleB>=o.minMotionAngle&&angleA<=o.maxMotionAngle&&angleB<=o.maxMotionAngle)result.push_back({a,b});
    }
    ensure(result.size()>=8,"insufficient relative rotation motions");return result;
}
Rigid handEye(std::vector<Motion> selected,const TrajectoryOptions& o){
    Rigid e;
    // AX=XB: log(R_A)=R_E log(R_B). Reuse the existing Horn solver with
    // +/- vectors (zero centroids) rather than introduce another rotation solver.
    for(int pass=0;pass<3;++pass){
        std::vector<PointCorrespondence> vectors;
        for(const auto& m:selected){auto a=logRotation(m.a.rotation),b=logRotation(m.b.rotation);vectors.push_back({b,a});vectors.push_back({scale(b,-1),scale(a,-1)});}
        e.rotation=solveRigidAlignment(vectors).transform.rotation;
        Matrix normal{};Vec rhs{};
        for(const auto& m:selected){
            Matrix c{};for(int j=0;j<3;++j){Vec unit{};unit[j]=1;auto col=sub(rotate(m.a.rotation,unit),unit);for(int i=0;i<3;++i)c[i][j]=col[i];}
            // (R_A-I)t_E = R_E t_B-t_A.
            Vec d=sub(rotate(e.rotation,m.b.translation),m.a.translation);
            for(int row=0;row<3;++row){outer(normal,c[row],c[row]);rhs=add(rhs,scale(c[row],d[row]));}
        }
        e.translation=linearSolve(normal,rhs,o.minTranslationPivotRatio);
        if(pass==2)break;
        std::vector<double> errors;for(const auto& m:selected)errors.push_back(norm(sub(logRotation(m.a.rotation),rotate(e.rotation,logRotation(m.b.rotation)))));
        double limit=std::max(o.motionTrimFloor,o.motionTrimMultiplier*quantile(errors,.5));
        std::vector<Motion> keep;for(std::size_t i=0;i<selected.size();++i)if(errors[i]<=limit)keep.push_back(selected[i]);
        ensure(keep.size()>=8,"insufficient robust hand-eye motions");selected=std::move(keep);
    }return e;
}
Rigid worldMean(const std::vector<TrajectoryPair>& pairs,const Rigid& e,const TrajectoryOptions& o){
    std::vector<Rigid> worlds;for(const auto& p:pairs)worlds.push_back(composeRigid(composeRigid(p.reference.pose,inverseRigid(e)),inverseRigid(p.source.pose)));
    // Medoid seed prevents a small set of gross outliers choosing the sign/mean.
    std::size_t best=0;double score=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<worlds.size();++i){double sum=0;for(const auto& w:worlds)sum+=std::min(4.0,norm(sub(worlds[i].translation,w.translation))/o.inlierPosition+rotationDistance(worlds[i].rotation,w.rotation)/o.inlierOrientation);if(sum<score){score=sum;best=i;}}
    Quat q{};Vec t{};double weight=0;
    for(const auto& w:worlds){double error=norm(sub(w.translation,worlds[best].translation))/o.inlierPosition+rotationDistance(w.rotation,worlds[best].rotation)/o.inlierOrientation;double a=error<=1?1:1/error;auto aligned=continuous(w.rotation,worlds[best].rotation);for(int k=0;k<4;++k)q[k]+=a*aligned[k];t=add(t,scale(w.translation,a));weight+=a;}
    return {normalized(q),scale(t,1/weight)};
}
std::vector<TrajectoryPair> boundedPairs(const std::vector<TrajectoryPair>& pairs,std::size_t bound){
    if(pairs.size()<=bound)return pairs;std::vector<TrajectoryPair> result;
    for(std::size_t i=0;i<bound;++i)result.push_back(pairs[i*(pairs.size()-1)/(bound-1)]);return result;
}
}
Rigid composeRigid(const Rigid& a,const Rigid& b){return {normalized(mul(a.rotation,b.rotation)),add(a.translation,rotate(a.rotation,b.translation))};}
Rigid inverseRigid(const Rigid& v){auto q=inverse(v.rotation);return {q,rotate(q,scale(v.translation,-1))};}
double rotationDistance(Quat a,Quat b){return norm(logRotation(mul(inverse(a),b)));}
void validateTrajectoryOptions(const TrajectoryOptions& o){
    ensure(std::isfinite(o.durationSeconds)&&o.durationSeconds>=5&&o.durationSeconds<=15,"trajectory duration must be 5..15 seconds");
    ensure(o.minSamples>=8&&o.maxSamples>=o.minSamples&&o.maxSamples<=16384&&o.maxSolveSamples>=o.minSamples&&o.maxSolveSamples<=512&&o.maxMotions>=8&&o.maxMotions<=96,"invalid trajectory sample bounds");
    ensure(o.maxPairingDeltaNs>0&&o.maxPairingDeltaNs<=100000000&&o.maxAgeNs>0&&o.maxAgeNs<=500000000,"invalid trajectory timing bounds");
    ensure(o.qualityRotationStepNs>0&&o.qualityRotationStepNs<=1000000000,"invalid rotation quality interval");
    for(auto x:o.minExtent)ensure(std::isfinite(x)&&x>0,"invalid translation excitation limit");
    for(auto x:{o.minSpanSeconds,o.minValidRatio,o.minRotation,o.minAxisDiversity,o.minMotionAngle,o.maxMotionAngle,o.minTranslationPivotRatio,o.motionTrimFloor,o.motionTrimMultiplier,o.inlierPosition,o.inlierOrientation,o.minInlierRatio,o.maxRmsPosition,o.maxRmsOrientation})ensure(std::isfinite(x)&&x>0,"invalid trajectory quality limit");
    ensure(o.minSpanSeconds<=o.durationSeconds&&o.minValidRatio<=1&&o.minInlierRatio<=1&&o.minAxisDiversity<=1&&o.minMotionAngle<o.maxMotionAngle&&o.maxMotionAngle<3.14159,"invalid trajectory quality range");
}
std::vector<TrajectoryPair> pairTrajectory(const std::vector<TimedCalibrationPose>& source,const std::vector<TimedCalibrationPose>& reference,const TrajectoryOptions& o){
    validateTrajectoryOptions(o);ensure(source.size()<=o.maxSamples&&reference.size()<=o.maxSamples,"trajectory recording bound exceeded");
    for(const auto* series:{&source,&reference}){std::int64_t prev=-1;for(const auto& s:*series){ensure(s.timeNs>=0&&s.timeNs>prev,"non-monotonic trajectory timestamps");prev=s.timeNs;if(s.valid)ensure(finite(s.pose),"invalid trajectory pose");}}
    std::vector<TrajectoryPair> result;
    for(const auto& s:source){if(!s.valid)continue;
        auto it=std::lower_bound(reference.begin(),reference.end(),s.timeNs,[](const auto& r,std::int64_t t){return r.timeNs<t;});
        auto best=it;if(it!=reference.begin()){auto prev=it-1;if(it==reference.end()||s.timeNs-prev->timeNs<=it->timeNs-s.timeNs)best=prev;}
        // Never jump across an invalid/lost reference to find an older valid pose.
        if(best!=reference.end()&&best->valid&&std::abs(best->timeNs-s.timeNs)<=o.maxPairingDeltaNs)result.push_back({s,*best});
    }return result;
}
TrajectoryQuality trajectoryQuality(const std::vector<TrajectoryPair>& pairs,double validRatio,const TrajectoryOptions& o){
    validateTrajectoryOptions(o);TrajectoryQuality q;q.samples=pairs.size();q.validRatio=validRatio;
    if(pairs.empty()){q.reason="insufficient motion: no valid timestamp pairs";return q;}
    q.spanSeconds=double(pairs.back().source.timeNs-pairs.front().source.timeNs)/1e9;
    for(int k=0;k<3;++k){std::vector<double> values;for(const auto& p:pairs)values.push_back(p.source.pose.translation[k]);q.extent[k]=quantile(values,.95)-quantile(values,.05);}
    Matrix axes{};double trace=0;auto previous=pairs.front().source;
    for(const auto& p:pairs)if(p.source.timeNs-previous.timeNs>=o.qualityRotationStepNs){
        auto v=logRotation(mul(inverse(previous.pose.rotation),p.source.pose.rotation));double a=norm(v);q.rotation+=a;
        if(a>=o.minMotionAngle/2){auto axis=scale(v,1/a);outer(axes,axis,axis);trace+=1;}previous=p.source;
    }
    q.axisDiversity=trace>0?std::max(0.0,27*determinant(axes)/(trace*trace*trace)):0;
    q.sufficient=pairs.size()>=o.minSamples&&std::isfinite(validRatio)&&validRatio>=o.minValidRatio&&validRatio<=1&&q.spanSeconds>=o.minSpanSeconds&&q.rotation>=o.minRotation&&q.axisDiversity>=o.minAxisDiversity;
    for(int k=0;k<3;++k)q.sufficient=q.sufficient&&q.extent[k]>=o.minExtent[k];
    q.reason=q.sufficient?"sufficient 6DoF excitation":"insufficient motion: count/valid ratio/span/XYZ extent/rotation/axis diversity";return q;
}
TrajectoryResiduals trajectoryResiduals(const std::vector<TrajectoryPair>& pairs,const Rigid& w,const Rigid& e){
    ensure(!pairs.empty(),"no residual samples");TrajectoryResiduals r;
    for(const auto& p:pairs){auto predicted=composeRigid(composeRigid(w,p.source.pose),e);double t=norm(sub(predicted.translation,p.reference.pose.translation)),a=rotationDistance(predicted.rotation,p.reference.pose.rotation);r.rmsPosition+=t*t;r.rmsOrientation+=a*a;r.maxPosition=std::max(r.maxPosition,t);r.maxOrientation=std::max(r.maxOrientation,a);}
    r.rmsPosition=std::sqrt(r.rmsPosition/pairs.size());r.rmsOrientation=std::sqrt(r.rmsOrientation/pairs.size());return r;
}
TrajectoryResult solveTrajectory(const std::vector<TrajectoryPair>& pairs,double validRatio,const TrajectoryOptions& o){
    validateTrajectoryOptions(o);ensure(pairs.size()<=o.maxSamples,"trajectory solve bound exceeded");
    std::int64_t previous=-1;for(const auto& p:pairs){ensure(p.source.valid&&p.reference.valid&&finite(p.source.pose)&&finite(p.reference.pose),"invalid solve pair");ensure(p.source.timeNs>previous&&p.reference.timeNs>=0&&std::abs(p.source.timeNs-p.reference.timeNs)<=o.maxPairingDeltaNs,"invalid solve timing");previous=p.source.timeNs;}
    TrajectoryResult result;result.quality=trajectoryQuality(pairs,validRatio,o);ensure(result.quality.sufficient,result.quality.reason.c_str());
    auto selected=boundedPairs(pairs,o.maxSolveSamples);
    for(int pass=0;pass<3;++pass){
        result.extrinsic=handEye(motions(selected,o),o);result.world=worldMean(selected,result.extrinsic,o);
        std::vector<TrajectoryPair> inliers;
        for(const auto& p:pairs){auto predicted=composeRigid(composeRigid(result.world,p.source.pose),result.extrinsic);if(norm(sub(predicted.translation,p.reference.pose.translation))<=o.inlierPosition&&rotationDistance(predicted.rotation,p.reference.pose.rotation)<=o.inlierOrientation)inliers.push_back(p);}
        ensure(inliers.size()>=o.minSamples&&double(inliers.size())/pairs.size()>=o.minInlierRatio,"trajectory residuals rejected too many samples");
        auto quality=trajectoryQuality(inliers,validRatio*inliers.size()/pairs.size(),o);ensure(quality.sufficient,"insufficient motion among residual inliers");
        result.inliers=inliers.size();result.residuals=trajectoryResiduals(inliers,result.world,result.extrinsic);result.quality=quality;
        selected=boundedPairs(inliers,o.maxSolveSamples);
    }
    result.pairs=pairs.size();result.allResiduals=trajectoryResiduals(pairs,result.world,result.extrinsic);
    ensure(result.residuals.rmsPosition<=o.maxRmsPosition&&result.residuals.rmsOrientation<=o.maxRmsOrientation,"trajectory RMS exceeds software acceptance limits");return result;
}
void validateTrajectoryTarget(const Config& current,const WorldCalibrationTarget& target,const std::string& publisher){
    validateWorldCalibrationTarget(current,target);ensure(current.bridgeId==publisher,"calibration publisher changed");
    ensure(target.binding.spaceApproved,"trajectory requires exact input space approval");
    for(const auto& [key,b]:current.bindings)if(b.source==target.binding.source&&b.inputSpace==target.binding.inputSpace&&b.inputRevision==target.binding.inputRevision)
        ensure(b.profile==target.binding.profile&&b.worldSpace==target.binding.worldSpace&&b.worldRevision==target.binding.worldRevision,"shared calibration group has incompatible profile/world space");
}
TrajectoryRecorder::TrajectoryRecorder(const Config& c,const WorldCalibrationTarget& t,std::string ref,TrajectoryOptions o):target_(t),publisher_(c.bridgeId),referenceId_(std::move(ref)),options_(o){validateTrajectoryOptions(o);validateTrajectoryTarget(c,t,publisher_);ensure(!referenceId_.empty(),"reference serial required");}
void TrajectoryRecorder::require(bool ok,const char* reason){if(!failure_.empty())throw std::invalid_argument(failure_);if(!ok){failure_=reason;throw std::invalid_argument(failure_);}}
void TrajectoryRecorder::checkConfig(const Config& c){try{require(true,"");validateTrajectoryTarget(c,target_,publisher_);}catch(const std::exception& e){failure_=e.what();throw;}}
bool TrajectoryRecorder::observation(const c1::TrackerObservation& p,std::int64_t arrival){
    require(true,"");try{validateCalibrationObservationIdentity(p,target_,session_);}catch(const std::exception& e){failure_=e.what();throw;}
    if(clock_.empty())clock_=p.clock_id;require(clock_==p.clock_id,"observation clock changed");
    require(arrival>=0&&arrival>=lastArrival_,"arrival clock moved backward");lastArrival_=arrival;
    if(p.sequence<=sequence_)return false;sequence_=p.sequence;
    require(++attempts_<=options_.maxSamples,"observation recording capacity exceeded");
    require(p.timestamp_ns>=0&&p.sent_at_ns>=p.timestamp_ns,"invalid observation age");
    auto age=p.sent_at_ns-p.timestamp_ns;
    // Epochs are unrelated: fix age once at local arrival, never subtract the
    // backend timestamp from the OpenVR/local monotonic clock.
    if(age>options_.maxAgeNs||age>arrival)return false;
    auto time=arrival-age;
    if(!source_.empty()&&time<=source_.back().timeNs)return false;
    bool valid=p.modality=="full"&&p.tracking_state=="tracked"&&p.validity.position&&p.validity.orientation&&p.position&&p.orientation;
    Rigid pose;
    if(valid){pose={normalized(permute(*p.orientation,target_.profile.quaternionAxes)),permute(*p.position,target_.profile.positionAxes)};valid=finite(pose);}
    // Profile only. E estimates the physical holding offset; existing mount must
    // not be absorbed into W or overwritten with E.
    source_.push_back({time,pose,valid});return valid;
}
void TrajectoryRecorder::deviceState(const c1::ObservationDeviceState& s){
    require(true,"");c1::TrackerObservation p;p.source_id=s.source_id;p.device_id=s.device_id;p.coordinate_space=s.coordinate_space;p.session_id=s.session_id;
    try{validateCalibrationObservationIdentity(p,target_,session_);}catch(const std::exception& e){failure_=e.what();throw;}
    if(clock_.empty())clock_=s.clock_id;require(clock_==s.clock_id,"observation clock changed");
}
void TrajectoryRecorder::reference(TimedCalibrationPose p,std::string id){require(id==referenceId_,"reference identity changed");require(reference_.size()<options_.maxSamples,"reference recording capacity exceeded");require(p.timeNs>=0&&(reference_.empty()||p.timeNs>reference_.back().timeNs),"reference clock moved backward");if(p.valid)p.valid=finite(p.pose);reference_.push_back(p);}
std::vector<TrajectoryPair> TrajectoryRecorder::pairs()const{ensure(failure_.empty(),failure_.c_str());return pairTrajectory(source_,reference_,options_);}
double TrajectoryRecorder::ratio(std::size_t count)const{
    double input=attempts_?double(count)/attempts_:0;
    // Reciprocal timestamp coverage detects source disappearance without assuming
    // equal sample rates or counting every faster reference tick as a lost pose.
    double reference=reference_.empty()?0:double(pairTrajectory(reference_,source_,options_).size())/reference_.size();
    return std::min(input,reference);
}
TrajectoryQuality TrajectoryRecorder::quality()const{auto p=pairs();return trajectoryQuality(p,ratio(p.size()),options_);}
TrajectoryResult TrajectoryRecorder::solve()const{auto p=pairs();return solveTrajectory(p,ratio(p.size()),options_);}
} // namespace mb

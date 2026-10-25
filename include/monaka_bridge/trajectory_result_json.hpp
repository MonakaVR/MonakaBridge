#pragma once
#include "trajectory_calibration.hpp"
#include <nlohmann/json.hpp>

namespace mb {
// The native solver owns every metric. Consumers only parse/present this JSON.
inline nlohmann::json trajectoryResultJson(const TrajectoryResult& r) {
    auto rigid=[](const Rigid& v) { return nlohmann::json{{"rotation_xyzw",v.rotation},{"translation_m",v.translation}}; };
    auto residual=[](const TrajectoryResiduals& v) {
        for(double x:{v.rmsPosition,v.maxPosition,v.rmsOrientation,v.maxOrientation})
            if(!std::isfinite(x)||x<0)throw std::runtime_error("NONFINITE_RESIDUAL");
        return nlohmann::json{{"rms_position",v.rmsPosition},{"max_position",v.maxPosition},
            {"rms_orientation",v.rmsOrientation},{"max_orientation",v.maxOrientation}};
    };
    auto out=residual(r.residuals);
    out.update({{"mode","trajectory"},{"world",rigid(r.world)},{"extrinsic_not_mount",rigid(r.extrinsic)},
        {"pairs",r.pairs},{"inliers",r.inliers},{"all_residuals",residual(r.allResiduals)}});
    const auto& q=r.quality;const auto& o=r.options;
    out["quality"]={{"extent_m",q.extent},{"accumulated_rotation_rad",q.rotation},{"axis_diversity",q.axisDiversity},
        {"valid_ratio",q.validRatio},{"span_seconds",q.spanSeconds},{"samples",q.samples},{"sufficient",q.sufficient}};
    out["measurement_options"]={{"recording_seconds",o.durationSeconds},{"relative_motion_min_rad",o.minMotionAngle},
        {"relative_motion_max_rad",o.maxMotionAngle},{"inlier_position_m",o.inlierPosition},{"inlier_orientation_rad",o.inlierOrientation}};
    return out;
}
} // namespace mb

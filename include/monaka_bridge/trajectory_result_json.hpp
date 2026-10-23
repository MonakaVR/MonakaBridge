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
    return out;
}
} // namespace mb

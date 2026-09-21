#pragma once

#include "config.hpp"

#include <optional>
#include <vector>

namespace mb {

struct PointCorrespondence {
    Vec source;
    Vec reference;
};

struct RigidAlignmentResult {
    Rigid transform;
    double rmsResidual = 0;
    double maxResidual = 0;
    std::size_t pointCount = 0;
};

// Proper right-handed rigid alignment. Reflections and collinear/insufficient
// geometry are rejected rather than silently projected into a usable result.
RigidAlignmentResult solveRigidAlignment(const std::vector<PointCorrespondence>& points);

struct WorldCalibrationTarget {
    std::uint32_t mappingRevision = 0;
    Binding binding;
    Profile profile;
};

WorldCalibrationTarget selectWorldCalibrationTarget(
    const Config& config,
    const std::string& source,
    const std::string& device,
    const std::string& tracker,
    const std::string& inputSpace,
    std::uint32_t inputRevision);

// Validates exact identity/space/revision and locks the first source session.
// Returns the profile-normalized point before the world transform. Existing
// mount translation is included; mount rotation is intentionally untouched.
Vec calibrationSourcePoint(
    const c1::TrackerObservation& observation,
    const WorldCalibrationTarget& target,
    std::optional<std::string>& lockedSession);

void validateWorldCalibrationTarget(
    const Config& current,
    const WorldCalibrationTarget& measured);

// Returns a candidate without performing I/O. measure-only returns an unchanged
// copy. Apply updates world transforms for the existing shared input-map group
// and increments mapping_revision exactly once.
Config worldCalibrationCandidate(
    const Config& current,
    const WorldCalibrationTarget& measured,
    const Rigid& transform,
    bool apply);

} // namespace mb

#include "monaka_bridge/world_calibration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mb {
namespace {

Vec subtract(Vec a, const Vec& b) {
    for (std::size_t i = 0; i < a.size(); ++i) a[i] -= b[i];
    return a;
}

double dot(const Vec& a, const Vec& b) {
    double value = 0;
    for (std::size_t i = 0; i < a.size(); ++i) value += a[i] * b[i];
    return value;
}

double normSquared(const Vec& value) { return dot(value, value); }

bool finite(const Vec& value) {
    return std::all_of(value.begin(), value.end(), [](double v) { return std::isfinite(v); });
}

bool finite(const Quat& value) {
    return std::all_of(value.begin(), value.end(), [](double v) { return std::isfinite(v); });
}

Vec centroid(const std::vector<PointCorrespondence>& points, bool source) {
    Vec result{};
    for (const auto& point : points) result = add(result, source ? point.source : point.reference);
    return scale(result, 1.0 / static_cast<double>(points.size()));
}

void requireNonCollinear(const std::vector<PointCorrespondence>& points, bool source) {
    double maximumDistanceSquared = 0;
    double maximumAreaSquared = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& a = source ? points[i].source : points[i].reference;
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            const auto& b = source ? points[j].source : points[j].reference;
            maximumDistanceSquared = std::max(maximumDistanceSquared, normSquared(subtract(b, a)));
            for (std::size_t k = j + 1; k < points.size(); ++k) {
                const auto& c = source ? points[k].source : points[k].reference;
                maximumAreaSquared = std::max(
                    maximumAreaSquared,
                    normSquared(cross(subtract(b, a), subtract(c, a))));
            }
        }
    }
    if (!(maximumDistanceSquared > 1e-12) ||
        maximumAreaSquared <= maximumDistanceSquared * maximumDistanceSquared * 1e-12)
        throw std::invalid_argument("calibration points are degenerate or collinear");
}

double determinant(const std::array<std::array<double, 3>, 3>& matrix) {
    return matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
           matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
           matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

Quat largestEigenQuaternion(const std::array<std::array<double, 3>, 3>& s) {
    // Horn/Davenport symmetric matrix in [w,x,y,z] order.
    std::array<std::array<double, 4>, 4> a{{
        {{s[0][0] + s[1][1] + s[2][2], s[1][2] - s[2][1], s[2][0] - s[0][2], s[0][1] - s[1][0]}},
        {{s[1][2] - s[2][1], s[0][0] - s[1][1] - s[2][2], s[0][1] + s[1][0], s[0][2] + s[2][0]}},
        {{s[2][0] - s[0][2], s[0][1] + s[1][0], -s[0][0] + s[1][1] - s[2][2], s[1][2] + s[2][1]}},
        {{s[0][1] - s[1][0], s[0][2] + s[2][0], s[1][2] + s[2][1], -s[0][0] - s[1][1] + s[2][2]}}
    }};
    std::array<std::array<double, 4>, 4> eigenvectors{};
    for (std::size_t i = 0; i < 4; ++i) eigenvectors[i][i] = 1;

    // Jacobi diagonalization avoids power iteration selecting the eigenvalue
    // with largest magnitude instead of the algebraically largest eigenvalue.
    for (int iteration = 0; iteration < 64; ++iteration) {
        std::size_t p = 0, q = 1;
        double largest = std::abs(a[p][q]);
        for (std::size_t i = 0; i < 4; ++i) for (std::size_t j = i + 1; j < 4; ++j)
            if (std::abs(a[i][j]) > largest) { largest = std::abs(a[i][j]); p = i; q = j; }
        if (largest < 1e-15) break;
        const double angle = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
        const double c = std::cos(angle), sValue = std::sin(angle);
        for (std::size_t k = 0; k < 4; ++k) {
            const double apk = a[p][k], aqk = a[q][k];
            a[p][k] = c * apk - sValue * aqk;
            a[q][k] = sValue * apk + c * aqk;
        }
        for (std::size_t k = 0; k < 4; ++k) {
            const double akp = a[k][p], akq = a[k][q];
            a[k][p] = c * akp - sValue * akq;
            a[k][q] = sValue * akp + c * akq;
        }
        for (std::size_t k = 0; k < 4; ++k) {
            const double vkp = eigenvectors[k][p], vkq = eigenvectors[k][q];
            eigenvectors[k][p] = c * vkp - sValue * vkq;
            eigenvectors[k][q] = sValue * vkp + c * vkq;
        }
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < 4; ++i) if (a[i][i] > a[best][best]) best = i;
    return normalized({eigenvectors[1][best], eigenvectors[2][best],
                       eigenvectors[3][best], eigenvectors[0][best]});
}

bool sameRigid(const Rigid& a, const Rigid& b) {
    return a.rotation == b.rotation && a.translation == b.translation;
}

bool sameProfile(const Profile& a, const Profile& b) {
    return a.convention == b.convention && a.positionAxes == b.positionAxes &&
           a.quaternionAxes == b.quaternionAxes && a.approved == b.approved &&
           a.angularSpaceVerified == b.angularSpaceVerified && a.evidence == b.evidence;
}

bool sameBinding(const Binding& a, const Binding& b) {
    return a.source == b.source && a.device == b.device && a.tracker == b.tracker &&
           a.profile == b.profile && a.inputSpace == b.inputSpace &&
           a.inputRevision == b.inputRevision && a.worldSpace == b.worldSpace &&
           a.worldRevision == b.worldRevision && a.spaceApproved == b.spaceApproved &&
           sameRigid(a.world, b.world) && sameRigid(a.mount, b.mount);
}

} // namespace

RigidAlignmentResult solveRigidAlignment(const std::vector<PointCorrespondence>& points) {
    if (points.size() < 3) throw std::invalid_argument("at least three correspondence points are required");
    for (const auto& point : points)
        if (!finite(point.source) || !finite(point.reference))
            throw std::invalid_argument("calibration points must be finite");
    requireNonCollinear(points, true);
    requireNonCollinear(points, false);

    const Vec sourceCenter = centroid(points, true);
    const Vec referenceCenter = centroid(points, false);
    std::array<std::array<double, 3>, 3> covariance{};
    double covarianceScaleSquared = 0;
    for (const auto& point : points) {
        const Vec source = subtract(point.source, sourceCenter);
        const Vec reference = subtract(point.reference, referenceCenter);
        for (std::size_t row = 0; row < 3; ++row) for (std::size_t column = 0; column < 3; ++column)
            covariance[row][column] += source[row] * reference[column];
    }
    for (const auto& row : covariance) for (double value : row) covarianceScaleSquared += value * value;
    const double covarianceScale = std::sqrt(covarianceScaleSquared);
    if (determinant(covariance) < -std::pow(covarianceScale, 3) * 1e-12)
        throw std::invalid_argument("reflected correspondence geometry is not allowed");

    RigidAlignmentResult result;
    result.transform.rotation = largestEigenQuaternion(covariance);
    result.transform.translation = subtract(referenceCenter, rotate(result.transform.rotation, sourceCenter));
    result.pointCount = points.size();
    double squaredResidual = 0;
    for (const auto& point : points) {
        const double residual = norm(subtract(
            add(rotate(result.transform.rotation, point.source), result.transform.translation),
            point.reference));
        squaredResidual += residual * residual;
        result.maxResidual = std::max(result.maxResidual, residual);
    }
    result.rmsResidual = std::sqrt(squaredResidual / static_cast<double>(points.size()));
    return result;
}

WorldCalibrationTarget selectWorldCalibrationTarget(
    const Config& config,
    const std::string& source,
    const std::string& device,
    const std::string& tracker,
    const std::string& inputSpace,
    std::uint32_t inputRevision) {
    const auto binding = config.bindings.find({source, device});
    if (binding == config.bindings.end()) throw std::invalid_argument("source/device mapping not found");
    if (binding->second.tracker != tracker || binding->second.inputSpace != inputSpace ||
        binding->second.inputRevision != inputRevision)
        throw std::invalid_argument("tracker/input-space/revision selection does not match mapping");
    const auto profile = config.profiles.find(binding->second.profile);
    if (profile == config.profiles.end() || !profile->second.approved || profile->second.evidence.empty())
        throw std::invalid_argument("selected mapping does not have an approved coordinate profile");
    return {config.revision, binding->second, profile->second};
}

Vec calibrationSourcePoint(
    const c1::TrackerObservation& observation,
    const WorldCalibrationTarget& target,
    std::optional<std::string>& lockedSession) {
    const auto& binding = target.binding;
    if (observation.source_id != binding.source || observation.device_id != binding.device)
        throw std::invalid_argument("observation identity changed during calibration");
    if (observation.coordinate_space.id != binding.inputSpace ||
        observation.coordinate_space.revision != binding.inputRevision ||
        observation.coordinate_space.convention != target.profile.convention)
        throw std::invalid_argument("observation input space/revision changed during calibration");
    if (!lockedSession) lockedSession = observation.session_id;
    else if (*lockedSession != observation.session_id)
        throw std::invalid_argument("observation session changed during calibration");
    if (observation.modality != "full" || !observation.validity.position || !observation.position)
        throw std::invalid_argument("calibration requires a valid full position observation");

    Vec point = permute(*observation.position, target.profile.positionAxes);
    if (norm(binding.mount.translation) > 0) {
        if (!observation.validity.orientation || !observation.orientation)
            throw std::invalid_argument("mount offset requires valid orientation during calibration");
        const Quat orientation = normalized(permute(*observation.orientation, target.profile.quaternionAxes));
        point = add(point, rotate(orientation, binding.mount.translation));
    }
    return point;
}

void validateWorldCalibrationTarget(const Config& current, const WorldCalibrationTarget& measured) {
    if (current.revision != measured.mappingRevision)
        throw std::invalid_argument("mapping_revision changed during calibration");
    const auto binding = current.bindings.find({measured.binding.source, measured.binding.device});
    if (binding == current.bindings.end() || !sameBinding(binding->second, measured.binding))
        throw std::invalid_argument("calibration mapping changed during measurement");
    const auto profile = current.profiles.find(measured.binding.profile);
    if (profile == current.profiles.end() || !sameProfile(profile->second, measured.profile))
        throw std::invalid_argument("coordinate profile changed during measurement");
}

Config worldCalibrationCandidate(
    const Config& current,
    const WorldCalibrationTarget& measured,
    const Rigid& transform,
    bool apply) {
    validateWorldCalibrationTarget(current, measured);
    if (!apply) return current;
    if (!finite(transform.rotation) || !finite(transform.translation))
        throw std::invalid_argument("world calibration transform must be finite");
    Config candidate = current;
    for (auto& [key, binding] : candidate.bindings)
        if (binding.source == measured.binding.source &&
            binding.inputSpace == measured.binding.inputSpace &&
            binding.inputRevision == measured.binding.inputRevision)
            binding.world = transform;
    if (candidate.revision == std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("mapping_revision exhausted");
    ++candidate.revision;
    validateConfig(candidate);
    return candidate;
}

} // namespace mb

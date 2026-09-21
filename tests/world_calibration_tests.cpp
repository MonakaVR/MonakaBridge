#include "helpers.hpp"
#include "monaka_bridge/world_calibration.hpp"

#include <iostream>

namespace {

void close(const mb::Vec& actual, const mb::Vec& expected, double tolerance = 1e-6) {
    for (std::size_t i = 0; i < actual.size(); ++i) CHECK(std::abs(actual[i] - expected[i]) <= tolerance);
}

void sameRotation(mb::Quat actual, mb::Quat expected, double tolerance = 1e-6) {
    actual = mb::normalized(actual); expected = mb::normalized(expected);
    double dot = 0; for (std::size_t i = 0; i < actual.size(); ++i) dot += actual[i] * expected[i];
    CHECK(std::abs(std::abs(dot) - 1.0) <= tolerance);
}

std::vector<mb::Vec> geometry() {
    return {{0,0,0},{1,0,0},{0,2,0},{0,0,3},{1,-2,2}};
}

std::vector<mb::PointCorrespondence> transformed(const mb::Rigid& transform, double noise = 0) {
    std::vector<mb::PointCorrespondence> points;
    std::size_t index = 0;
    for (const auto& source : geometry()) {
        auto reference = mb::add(mb::rotate(transform.rotation, source), transform.translation);
        if (noise) {
            reference[0] += noise * (static_cast<int>(index % 3) - 1);
            reference[1] += noise * (static_cast<int>((index + 1) % 3) - 1);
            reference[2] += noise * (static_cast<int>((index + 2) % 3) - 1);
        }
        points.push_back({source, reference}); ++index;
    }
    return points;
}

template<class Function> void rejected(Function function) {
    bool failed = false; try { function(); } catch (const std::invalid_argument&) { failed = true; }
    CHECK(failed);
}

} // namespace

int main() try {
    const mb::Rigid translation{{0,0,0,1},{2,-3,4}};
    auto solved = mb::solveRigidAlignment(transformed(translation));
    sameRotation(solved.transform.rotation, translation.rotation);
    close(solved.transform.translation, translation.translation);
    CHECK(solved.pointCount == 5 && solved.rmsResidual < 1e-9 && solved.maxResidual < 1e-9);

    const auto axis = mb::normalized(mb::Quat{.2,-.3,.4,.8});
    const mb::Rigid arbitrary{axis,{-1,.5,2.25}};
    solved = mb::solveRigidAlignment(transformed(arbitrary));
    sameRotation(solved.transform.rotation, arbitrary.rotation);
    close(solved.transform.translation, arbitrary.translation);

    const double yaw = .7;
    const mb::Rigid yawTransform{{0,std::sin(yaw/2),0,std::cos(yaw/2)},{.25,.5,-.75}};
    solved = mb::solveRigidAlignment(transformed(yawTransform));
    sameRotation(solved.transform.rotation, yawTransform.rotation);
    close(solved.transform.translation, yawTransform.translation);

    solved = mb::solveRigidAlignment(transformed(arbitrary, 0.0005));
    sameRotation(solved.transform.rotation, arbitrary.rotation, .002);
    close(solved.transform.translation, arbitrary.translation, .002);
    CHECK(solved.rmsResidual < .002 && solved.maxResidual < .003);

    const std::vector<mb::PointCorrespondence> three{
        {{0,0,0},{1,2,3}}, {{1,0,0},{2,2,3}}, {{0,1,0},{1,3,3}}};
    solved = mb::solveRigidAlignment(three);
    close(solved.transform.translation,{1,2,3});

    rejected([] { mb::solveRigidAlignment({{{0,0,0},{0,0,0}},{{1,0,0},{1,0,0}}}); });
    rejected([] { mb::solveRigidAlignment({
        {{0,0,0},{0,0,0}},{{1,0,0},{1,0,0}},{{2,0,0},{2,0,0}}}); });
    rejected([] {
        std::vector<mb::PointCorrespondence> reflection;
        for (const auto& source : geometry()) reflection.push_back({source,{-source[0],source[1],source[2]}});
        mb::solveRigidAlignment(reflection);
    });

    auto configValue = config();
    auto& binding = configValue.bindings.at({"pico","device"});
    binding.mount.translation={.01,.02,.03};
    auto shared=binding;shared.device="device-shared";shared.tracker="logical-shared";
    configValue.bindings[{shared.source,shared.device}]=shared;
    const auto target = mb::selectWorldCalibrationTarget(
        configValue,"pico","device","logical-pico","native",1);
    auto observation = sample(); observation.orientation=mb::Quat{0,0,0,1};
    std::optional<std::string> session;
    close(mb::calibrationSourcePoint(observation,target,session),{1.01,2.02,3.03});
    CHECK(session==S1);
    auto stale = observation; stale.coordinate_space.revision=2;
    rejected([&] { mb::calibrationSourcePoint(stale,target,session); });
    auto rebound = observation; rebound.device_id="different-device";
    rejected([&] { mb::calibrationSourcePoint(rebound,target,session); });
    auto newSession = observation; newSession.session_id=newSession.clock_id=S2;
    rejected([&] { mb::calibrationSourcePoint(newSession,target,session); });

    const auto originalMount = binding.mount;
    const auto originalProfile = configValue.profiles.at(binding.profile);
    const auto measuredOnly = mb::worldCalibrationCandidate(configValue,target,arbitrary,false);
    CHECK(measuredOnly.revision==configValue.revision);
    CHECK(measuredOnly.bindings.at({"pico","device"}).world.rotation==binding.world.rotation);
    auto concurrent = configValue; ++concurrent.revision;
    rejected([&] { mb::worldCalibrationCandidate(concurrent,target,arbitrary,true); });

    const auto applied = mb::worldCalibrationCandidate(configValue,target,arbitrary,true);
    CHECK(applied.revision==configValue.revision+1);
    const auto& updated=applied.bindings.at({"pico","device"});
    sameRotation(updated.world.rotation,arbitrary.rotation);close(updated.world.translation,arbitrary.translation);
    CHECK(updated.mount.rotation==originalMount.rotation&&updated.mount.translation==originalMount.translation);
    CHECK(updated.spaceApproved==binding.spaceApproved&&updated.worldRevision==binding.worldRevision);
    CHECK(applied.profiles.at(binding.profile).positionAxes==originalProfile.positionAxes);
    CHECK(applied.profiles.at(binding.profile).quaternionAxes==originalProfile.quaternionAxes);
    // Existing shared input-map semantics update same-source siblings, not another source.
    sameRotation(applied.bindings.at({"pico","device-shared"}).world.rotation,arbitrary.rotation);
    const auto& sibling=applied.bindings.at({"other","device"});
    CHECK(sibling.world.rotation==mb::Quat({0,0,0,1}));

    std::cout << "PASS world calibration: rigid recovery, translation/yaw/3-axis/noise, geometry/reflection rejection, identity/revision guards, measure/apply semantics\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

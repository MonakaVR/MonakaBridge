#pragma once
#include "static_calibration_session.hpp"

namespace mb::local_calibration {
std::int64_t utcMilliseconds();
// Reads an operator-adopted, hash-pinned policy/evidence set on every check.
// Construction never adopts criteria, writes config or acquires capture resources.
std::shared_ptr<const OutputApprovalAuthority> makeProductionOutputApprovalAuthority(
 const std::filesystem::path& root,Clock wall=utcMilliseconds,Clock monotonic=steadyNow);
}

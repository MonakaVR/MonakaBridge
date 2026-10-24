#pragma once
#include "static_calibration_session.hpp"

namespace mb::local_calibration {
std::int64_t utcMilliseconds();
// One semantic reuse decision. Declarations are operator-adopted local evidence;
// live freshness is independently supplied by the native capture owner.
Json evaluateEvidence(const OutputApprovalContext&,const Json& savedEvidence,
 const Json& policy,const Json& currentDeclaration,std::int64_t wallNow);
// Reads an operator-adopted, hash-pinned policy/evidence set on every check.
// Construction never adopts criteria, writes config or acquires capture resources.
std::shared_ptr<const OutputApprovalAuthority> makeProductionOutputApprovalAuthority(
 const std::filesystem::path& root,Clock wall=utcMilliseconds,Clock monotonic=steadyNow);
}

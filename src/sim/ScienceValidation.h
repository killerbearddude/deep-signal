#pragma once
// Strict science graph/work/provenance validation without reading hidden geology.
#include "sim/GameState.h"
namespace deep {
// Rejects invalid catalog, acquisitions, leases, labor, findings and revisions.
// Never repairs, re-samples, completes work or publishes an assessment.
void validateScienceState(const GameState&);
} // namespace deep

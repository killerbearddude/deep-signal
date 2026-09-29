#pragma once

// Atomic registration plus development, or additive development of an existing
// site. Assignments may change; the admitted package and route never do.
#include "sim/SiteDevelopmentProgram.h"
namespace deep {
struct NewResourceSite {
    BodyId bodyId;
    std::string name;
    std::optional<InstitutionId> institutionId;
    SiteOperatingPolicy operatingPolicy;
};
struct CreateSiteDevelopmentCommand {
    SiteDevelopmentCharter charter;
    // Present requires an unassigned charter.siteId; command assigns both IDs.
    std::optional<NewResourceSite> newSite;
};
struct AmendSiteDevelopmentCommand {
    SiteDevelopmentProgramId programId;
    SiteDevelopmentAssignments assignments;
};
struct SuspendSiteDevelopmentCommand {
    SiteDevelopmentProgramId programId;
};
struct ResumeSiteDevelopmentCommand {
    SiteDevelopmentProgramId programId;
};
struct CancelSiteDevelopmentCommand {
    SiteDevelopmentProgramId programId;
};
struct AcknowledgeSiteDevelopmentIssueCommand {
    SiteDevelopmentProgramId programId;
    std::string signature;
};
} // namespace deep

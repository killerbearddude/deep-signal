#pragma once

// Command boundary for the optional technical-development workflow. Authoring
// records durable intent; daily execution owns all work, material, and artifacts.
#include "sim/TechnicalDevelopment.h"

namespace deep {

struct CreateTechnicalDevelopmentCommand {
    TechnicalDevelopmentCharter charter;
};
struct AmendTechnicalDevelopmentCommand {
    TechnicalDevelopmentProgramId programId;
    TechnicalDevelopmentCharter charter;
};
struct SuspendTechnicalDevelopmentCommand {
    TechnicalDevelopmentProgramId programId;
};
struct ResumeTechnicalDevelopmentCommand {
    TechnicalDevelopmentProgramId programId;
};
struct CancelTechnicalDevelopmentCommand {
    TechnicalDevelopmentProgramId programId;
};
struct AcknowledgeTechnicalDevelopmentIssueCommand {
    TechnicalDevelopmentProgramId programId;
    std::string signature;
};

} // namespace deep

#pragma once

// Executes one bounded P5 engineering action per program opening and publishes
// durable artifacts/reports. Hidden candidate truth is read only by real tests.
#include "sim/Events.h"
#include "sim/ProgramControl.h"

#include <functional>

namespace deep {

struct TechnicalDevelopmentExecutionHooks {
    std::function<void(EventSeverity, SimEventPayload)> emit;
    std::function<void(std::size_t)> prepareEvents;
};

// Executes at most one positive stage action for this program's opening share.
void runTechnicalDevelopmentOpeningDay(GameState&, TechnicalDevelopmentProgram&, OpeningProgramContext&,
                                       const TechnicalDevelopmentExecutionHooks&);
// Applies end-of-day support qualification and publishes due owned reports.
void finishTechnicalDevelopmentDay(GameState&, const TechnicalDevelopmentExecutionHooks&);

} // namespace deep

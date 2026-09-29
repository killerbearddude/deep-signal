#pragma once
// Paid opening support funds one shared raw handler for freight and extraction.
// The physical extraction boundary alone samples true deposits.
#include "sim/ProgramControl.h"
#include "sim/Events.h"
#include <functional>
namespace deep {
struct SiteOperationHooks {
    std::function<void(EventSeverity, SimEventPayload)> emit;
    std::function<void(std::size_t)> prepareEvents;
};
void runSitesOpeningDay(GameState&, OpeningProgramContext&, const SiteOperationHooks&);
void runSitesExtractionDay(GameState&, OpeningProgramContext&, const SiteOperationHooks&);
void finishSitesDay(GameState&, const SiteOperationHooks&);
[[nodiscard]] std::string siteOperatingCondition(const GameState&, const ResourceSite&);
} // namespace deep

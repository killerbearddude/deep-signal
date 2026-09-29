#pragma once

// Pure known-data freight planning and shared conservation comparisons. Route
// estimates use existing prototype transit; payload mass never changes it.

#include "sim/GameState.h"

#include <optional>
#include <string>
#include <vector>

namespace deep {

struct OpeningProgramContext;

// Absolute plus relative tolerance is shared by transfer normalization,
// capacity comparisons, conservation validation, and completion decisions.
inline constexpr double kFreightAbsoluteTolerance = 1.0e-9;
inline constexpr double kFreightRelativeTolerance = 1.0e-10;
[[nodiscard]] bool freightNearlyEqual(double a, double b) noexcept;
[[nodiscard]] bool freightPositive(double amount) noexcept;

struct FreightHullCapability {
    ShipId shipId;
    double capacity = 0.0;
    double operationalHandlingPerDay = 0.0;
    double onboardQuantity = 0.0;
};

struct FreightShipmentPlan {
    std::vector<FreightManifestRow> manifest;
    double quantity = 0.0;
    double requiredFuel = 0.0;
    double additionalFuel = 0.0;
    std::int64_t loadingDays = 0;
    std::int64_t unloadingDays = 0;
    std::int64_t departureDay = 0;
    std::int64_t returnDepartureDay = 0;
    bool ready = false;
    std::string waitingReason;
};

[[nodiscard]] std::vector<FreightHullCapability> freightHullCapabilities(const GameState&, const Fleet&);
[[nodiscard]] double freightCargoAboard(const GameState&, FreightProgramId) noexcept;
[[nodiscard]] double freightShipmentPlannedQuantity(const FreightShipment&) noexcept;
[[nodiscard]] double freightShipmentLoadedQuantity(const FreightProgram&) noexcept;
[[nodiscard]] double freightUnpickedQuantity(const GameState&, const FreightProgram&) noexcept;
[[nodiscard]] double freightFleetFuel(const GameState&, const Fleet&) noexcept;
// Preflight the retained common roster-order debit and residual normalization,
// without changing manual/P3A payment behavior or mutating engine fuel.
[[nodiscard]] bool freightFuelDebitRepresentable(const GameState&, const Fleet&, double cost) noexcept;
[[nodiscard]] double freightFuelAllowanceRemaining(const FreightProgram&) noexcept;
[[nodiscard]] double freightEffectiveFloor(const FreightProgram&, ProcessedMaterial) noexcept;

// Admission rejects malformed structure, never absent execution capability.
// allowZeroQuantity is used for already amended persisted charters.
[[nodiscard]] std::optional<std::string>
validateFreightProgramCharter(const GameState&, const FreightProgramCharter&, bool allowZeroQuantity = false);
[[nodiscard]] FreightProgramAmendment freightAmendmentFromCharter(const FreightProgramCharter&);
void applyFreightAmendment(FreightProgramCharter&, const FreightProgramAmendment&);

// Bounded candidate search: one maximum candidate, one fuel-adjusted candidate,
// and at most 12 halvings. Every candidate validates its complete dated fuel and
// shared stock requirement. Failure to find a supported candidate is described
// as a bounded-planner limitation, not proof of physical impossibility.
// openingDay defaults to the current execution boundary; between-day previews
// supply the next day explicitly without copying or mutating GameState.
[[nodiscard]] FreightShipmentPlan planFreightShipment(const GameState&, const FreightProgram&, const Fleet&,
                                                      const OpeningProgramContext* = nullptr,
                                                      std::int64_t openingDay = -1);

// Reprices an actual or planned manifest at a supplied departure day; positive
// cargo on an unpowered/zero-rate hull has no finite unloading timetable.
[[nodiscard]] FreightShipmentPlan evaluateFreightManifest(const GameState&, const FreightProgram&,
                                                          const Fleet&,
                                                          const std::vector<FreightManifestRow>&,
                                                          std::int64_t departureDay);
[[nodiscard]] std::string freightProgramExecutionCondition(const GameState&, const FreightProgram&);

} // namespace deep

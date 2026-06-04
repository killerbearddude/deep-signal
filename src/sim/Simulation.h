#pragma once

// Declares the headless simulation facade for Prototype 0.1.
// Simulation owns one GameState and is the only component allowed to mutate it;
// callers interact through commands, returned events, and read-only snapshots.

#include "sim/Commands.h"
#include "sim/Error.h"
#include "sim/Events.h"
#include "sim/GameState.h"

#include <vector>

namespace deep {

// Single-owner, single-threaded simulation engine. This class intentionally has
// no UI, SDL, ImGui, SQLite, filesystem, or wall-clock dependencies.
class Simulation {
public:
    // Creates an empty simulation state. Primarily useful for tests that build
    // custom GameState values before exercising validation paths.
    Simulation() = default;

    // Takes ownership of an initial state snapshot, usually from ScenarioFactory
    // or the future SQLite repository layer.
    explicit Simulation(GameState initialState);

    // Returns a read-only snapshot reference. The reference remains valid until
    // the next non-const Simulation operation mutates internal vectors.
    [[nodiscard]] const GameState& state() const noexcept;

    // Validates and applies a command. Rejected commands append a warning event
    // but do not otherwise mutate gameplay state.
    CommandResult execute(const SimCommand& command);

    // Advances the simulation by a positive number of days and returns only the
    // events emitted during this call. All returned events are also appended to
    // GameState::eventLog.
    std::vector<SimEvent> advanceDays(int days);

private:
    // Movement duration is computed from deterministic sustained-burn transit
    // planning when each active order starts.

    GameState state_;

    CommandResult assignShipyardBuild(const AssignShipyardBuildCommand& command);
    CommandResult moveFleet(const MoveFleetCommand& command);
    CommandResult queueFleetMoveOrder(const QueueFleetMoveOrderCommand& command);
    CommandResult clearFleetOrderQueue(const ClearFleetOrderQueueCommand& command);
    CommandResult cancelFleetOrder(const CancelFleetOrderCommand& command);
    CommandResult resourceSurvey(const ResourceSurveyCommand& command);
    CommandResult assignAppointment(const AssignAppointmentCommand& command);
    CommandResult setColonyProcessingPolicy(const SetColonyProcessingPolicyCommand& command);

    // Starts the first queued order, if one exists and can be started from the
    // fleet's current body. emitted is null for command-time starts where events
    // are appended to GameState only; daily ticks pass their emitted-event list.
    bool startNextQueuedFleetOrder(Fleet& fleet, std::vector<SimEvent>* emitted);

    void simulateOneDay(std::vector<SimEvent>& emitted);
    void simulateMining(std::vector<SimEvent>& emitted);
    void simulateProcessing();
    void simulateShipyards(std::vector<SimEvent>& emitted);
    void simulateFleetMovement(std::vector<SimEvent>& emitted);

    [[nodiscard]] SimEvent makeEvent(EventSeverity severity, SimEventPayload payload);
    void emitEvent(std::vector<SimEvent>& emitted, EventSeverity severity, SimEventPayload payload);
    void appendEvent(EventSeverity severity, SimEventPayload payload);

    [[nodiscard]] Colony* findColony(ColonyId id) noexcept;
    [[nodiscard]] const Colony* findColony(ColonyId id) const noexcept;
    [[nodiscard]] Body* findBody(BodyId id) noexcept;
    [[nodiscard]] const Body* findBody(BodyId id) const noexcept;
    [[nodiscard]] ShipClass* findShipClass(ShipClassId id) noexcept;
    [[nodiscard]] const ShipClass* findShipClass(ShipClassId id) const noexcept;
    [[nodiscard]] Fleet* findFleet(FleetId id) noexcept;
    [[nodiscard]] const Fleet* findFleet(FleetId id) const noexcept;
    [[nodiscard]] const Institution* findInstitution(InstitutionId id) const noexcept;
    [[nodiscard]] const Person* findPerson(PersonId id) const noexcept;

    [[nodiscard]] ShipyardOrderId allocateShipyardOrderId() noexcept;
    [[nodiscard]] ShipId allocateShipId() noexcept;
    [[nodiscard]] FleetId allocateFleetId() noexcept;
    [[nodiscard]] EventId allocateEventId() noexcept;
};

} // namespace deep

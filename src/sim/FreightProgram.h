#pragma once

// Durable intent and custody history for the bounded two-colony freight loop.
// Quantities are normalized processed-material cargo units. Physical inventory
// lives only in colony stockpiles, ship cargo lots, and engine tanks; manifests
// below are finite planned limits, never a second inventory.

#include "sim/Domain.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep {

struct FreightProgramPolicy {
    double sourceCargoFloor = 0.0;
    double sourcePropellantFloor = 0.0;
    std::optional<double> maxAdditionalPropellant;
    double returnContingencyFraction = 0.0;
};

// This is the complete editable portion of a charter. Keeping route/material
// outside it prevents an amendment from redirecting committed physical goods.
struct FreightProgramAmendment {
    std::string name;
    double totalQuantity = 0.0;
    std::optional<FleetId> requestedFleetId;
    std::optional<PersonId> requestedLeaderId;
    FreightProgramPolicy policy;
};

struct FreightProgramCharter {
    std::string name;
    ColonyId sourceColonyId;
    ColonyId destinationColonyId;
    ProcessedMaterial material = ProcessedMaterial::StructuralAlloys;
    double totalQuantity = 0.0;
    std::optional<FleetId> requestedFleetId;
    std::optional<PersonId> requestedLeaderId;
    FreightProgramPolicy policy;
};

enum class FreightProgramLifecycle { Authorized, Suspended, Closing, Closed };
// Suspension retains this intention, especially Cancelled settlement authority.
enum class FreightProgramClosure { None, Completed, Cancelled };
enum class FreightProgramTask {
    None,
    Reposition,
    Preparing,
    Loading,
    Outbound,
    Unloading,
    Return,
    ReturningCargo
};
enum class FreightTransferKind { Load, Delivery, SourceReturn, OperatingFuel };

struct FreightManifestRow {
    ShipId shipId;
    // Zero rows retain the complete committed roster (including fuel-only hulls).
    double plannedQuantity = 0.0;
};

struct FreightShipment {
    int number = 1;
    int charterRevision = 1;
    std::int64_t committedDay = 0;
    FleetId fleetId;
    PersonId leaderId;
    ColonyId sourceColonyId;
    ColonyId destinationColonyId;
    ProcessedMaterial material = ProcessedMaterial::StructuralAlloys;
    std::vector<FreightManifestRow> manifest;
};

// One receipt per positive fleet transfer action. Sequence and shipment identity
// survive completion; actual per-hull custody remains on Ship::cargo.
struct FreightTransferReceipt {
    int sequence = 1;
    int shipmentNumber = 1;
    std::int64_t day = 0;
    FleetId fleetId;
    PersonId leaderId;
    ColonyId colonyId;
    ProcessedMaterial material = ProcessedMaterial::StructuralAlloys;
    FreightTransferKind kind = FreightTransferKind::Load;
    double amount = 0.0;
};

struct FreightProgramReport {
    std::int64_t startDay = 0;
    std::int64_t endDay = 0;
    bool isNinetyDayReview = false;
    int charterRevision = 1;
    double cargoLoaded = 0.0;
    double cargoDelivered = 0.0;
    double cargoReturned = 0.0;
    double fuelLoaded = 0.0;
    double fuelBurned = 0.0;
    double cargoAboard = 0.0;
    int shipmentsStarted = 0;
    double targetQuantity = 0.0;
    double cumulativeDelivered = 0.0;
    double committedQuantity = 0.0;
    std::optional<FleetId> fleetId;
    std::optional<BodyId> fleetBodyId;
    std::string waitingReason;
    // Historical authority snapshot; later amendments never rewrite the limits
    // under which this period's actual transfers and commitments were reported.
    FreightProgramPolicy policy{};
};

struct FreightProgramIssue {
    std::string signature;
    std::string message;
    bool acknowledged = true;
};

struct FreightProgram {
    FreightProgramId id;
    FreightProgramCharter charter;
    std::int64_t createdDay = 0;
    int charterRevision = 1;
    FreightProgramLifecycle lifecycle = FreightProgramLifecycle::Authorized;
    FreightProgramClosure closure = FreightProgramClosure::None;
    std::optional<std::int64_t> closedDay;
    // The only authoritative actual control record. A retained empty task can
    // temporarily have no lease after suspension; cargo custody cannot.
    std::optional<FleetId> leasedFleetId;
    FreightProgramTask task = FreightProgramTask::None;
    std::optional<FleetId> taskFleetId;
    std::optional<FreightShipment> shipment;
    int nextShipmentNumber = 1;
    std::vector<FreightTransferReceipt> receipts;
    double cargoLoaded = 0.0;
    double cargoDelivered = 0.0;
    double cargoReturned = 0.0;
    double fuelLoaded = 0.0;
    double fuelBurned = 0.0;
    std::int64_t nextReportDay = 30;
    std::int64_t reportStartDay = 0;
    double reportedCargoLoaded = 0.0;
    double reportedCargoDelivered = 0.0;
    double reportedCargoReturned = 0.0;
    double reportedFuelLoaded = 0.0;
    double reportedFuelBurned = 0.0;
    int reportedShipments = 0;
    std::vector<FreightProgramReport> reports;
    FreightProgramIssue issue;
};

} // namespace deep

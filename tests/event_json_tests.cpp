#include "save/EventJson.h"
#include "sim/Events.h"
#include "sim/Minerals.h"

// Direct regression tests for current-schema event JSON serialization.
// These tests intentionally exercise EventJson without SaveGameRepository so the
// payload grammar remains protected before any JSON dependency policy changes.

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

// Exception type used by the minimal CTest-compatible harness.
class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string_view message)
        : std::runtime_error{std::string{message}} {}
};

// Keeps assertion failures readable without adding a third-party test framework.
void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

// Event payloads store double values; exact text formatting is not part of the
// schema contract, so round-trip comparisons use a tight numeric tolerance.
bool almostEqual(const double lhs, const double rhs) noexcept {
    constexpr double kEpsilon = 1.0e-9;
    return std::fabs(lhs - rhs) <= kEpsilon;
}

// Compares strongly typed event payloads field-by-field. This prevents the test
// from depending on JSON member ordering or serializer-specific whitespace.
bool samePayload(const deep::SimEventPayload& lhs, const deep::SimEventPayload& rhs) {
    if (lhs.index() != rhs.index()) {
        return false;
    }

    return std::visit([](const auto& left, const auto& right) -> bool {
        using Left = std::decay_t<decltype(left)>;
        using Right = std::decay_t<decltype(right)>;
        if constexpr (!std::is_same_v<Left, Right>) {
            return false;
        } else if constexpr (std::is_same_v<Left, deep::MineralExtractedEvent>) {
            return left.colonyId == right.colonyId &&
                   left.bodyId == right.bodyId &&
                   left.mineral == right.mineral &&
                   almostEqual(left.amount, right.amount) &&
                   almostEqual(left.remainingDeposit, right.remainingDeposit);
        } else if constexpr (std::is_same_v<Left, deep::ShipyardOrderCreatedEvent>) {
            return left.orderId == right.orderId &&
                   left.colonyId == right.colonyId &&
                   left.shipClassId == right.shipClassId &&
                   left.quantity == right.quantity;
        } else if constexpr (std::is_same_v<Left, deep::ShipClassRevisionCreatedEvent>) {
            return left.shipClassId == right.shipClassId &&
                   left.basedOnClassId == right.basedOnClassId &&
                   left.revision == right.revision;
        } else if constexpr (std::is_same_v<Left, deep::ShipCompletedEvent>) {
            return left.orderId == right.orderId &&
                   left.colonyId == right.colonyId &&
                   left.shipId == right.shipId &&
                   left.fleetId == right.fleetId &&
                   left.shipClassId == right.shipClassId;
        } else if constexpr (std::is_same_v<Left, deep::FleetOrderAssignedEvent>) {
            return left.fleetId == right.fleetId &&
                   left.originBodyId == right.originBodyId &&
                   left.destinationBodyId == right.destinationBodyId &&
                   left.daysRemaining == right.daysRemaining;
        } else if constexpr (std::is_same_v<Left, deep::FleetArrivedEvent>) {
            return left.fleetId == right.fleetId &&
                   left.destinationBodyId == right.destinationBodyId;
        } else if constexpr (std::is_same_v<Left, deep::ResourceSurveyCompletedEvent>) {
            return left.fleetId == right.fleetId &&
                   left.bodyId == right.bodyId &&
                   left.observationBatchId == right.observationBatchId;
        } else if constexpr (std::is_same_v<Left, deep::SurveyProgramAuditEvent>) {
            return left.programId == right.programId && left.kind == right.kind &&
                   left.fleetId == right.fleetId && left.bodyId == right.bodyId &&
                   left.leaderId == right.leaderId && left.approach == right.approach &&
                   left.charterRevision == right.charterRevision &&
                   left.passNumber == right.passNumber &&
                   almostEqual(left.fuelAmount, right.fuelAmount) && left.detail == right.detail;
        } else if constexpr (std::is_same_v<Left, deep::FreightProgramAuditEvent>) {
            return left.programId == right.programId && left.kind == right.kind &&
                   left.fleetId == right.fleetId && left.colonyId == right.colonyId &&
                   left.leaderId == right.leaderId && left.charterRevision == right.charterRevision &&
                   left.shipmentNumber == right.shipmentNumber &&
                   almostEqual(left.amount, right.amount) && left.detail == right.detail;
        } else if constexpr (std::is_same_v<Left, deep::EquipmentDutyUsedEvent>) {
            return left.fleetId == right.fleetId && left.shipId == right.shipId &&
                   left.componentId == right.componentId && left.surveyProgramId == right.surveyProgramId &&
                   almostEqual(left.duty, right.duty) && almostEqual(left.beforeUsedDuty, right.beforeUsedDuty) &&
                   almostEqual(left.afterUsedDuty, right.afterUsedDuty);
        } else if constexpr (std::is_same_v<Left, deep::MaintenanceProgramAuditEvent>) {
            return left.programId == right.programId && left.kind == right.kind &&
                   left.jobNumber == right.jobNumber && left.detail == right.detail;
        } else if constexpr (std::is_same_v<Left, deep::AnalysisProgramAuditEvent>) {
            return left.programId==right.programId && left.kind==right.kind && left.jobId==right.jobId && left.detail==right.detail;
        } else if constexpr (std::is_same_v<Left, deep::CommandRejectedEvent>) {
            return left.reason == right.reason;
        }
    }, lhs, rhs);
}

void requireThrows(const std::string_view message, const auto& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }

    throw TestFailure{message};
}

void requireRoundTrip(const deep::SimEventPayload& payload, const std::string_view message) {
    // Round-trips through the public API only. This guards schema v1 field names,
    // typed ID reconstruction, and the optional/fallback JSON implementations.
    const std::string eventType = deep::save::eventTypeName(payload);
    const std::string json = deep::save::eventPayloadToJson(payload);
    const deep::SimEventPayload restored = deep::save::eventPayloadFromJson(eventType, json);
    require(samePayload(payload, restored), message);
}

void test_mineral_extracted_round_trips() {
    // Protects the daily mining audit payload used heavily by the current smoke
    // scenario and save/load event log.
    requireRoundTrip(deep::MineralExtractedEvent{
        .colonyId = deep::ColonyId{1},
        .bodyId = deep::BodyId{2},
        .mineral = deep::Mineral::Copper,
        .amount = 12.5,
        .remainingDeposit = 9876.25
    }, "mineral_extracted payload round-trips");
}

void test_shipyard_order_created_round_trips() {
    // Prevents regressions where accepted shipyard commands lose the requested
    // quantity or typed references when persisted.
    requireRoundTrip(deep::ShipyardOrderCreatedEvent{
        .orderId = deep::ShipyardOrderId{3},
        .colonyId = deep::ColonyId{4},
        .shipClassId = deep::ShipClassId{5},
        .quantity = 6
    }, "shipyard_order_created payload round-trips");
}

void test_ship_class_revision_created_round_trips() {
    requireRoundTrip(deep::ShipClassRevisionCreatedEvent{
        .shipClassId = deep::ShipClassId{7},
        .basedOnClassId = deep::ShipClassId{3},
        .revision = 2
    }, "ship_class_revision_created payload round-trips");
    requireRoundTrip(deep::ShipClassRevisionCreatedEvent{
        .shipClassId = deep::ShipClassId{8},
        .basedOnClassId = std::nullopt,
        .revision = 1
    }, "root revision event round-trips");
}

void test_ship_completed_round_trips() {
    // Ensures ship completion events retain every reference needed by future UI
    // drilldowns from event log to ship, fleet, class, and colony.
    requireRoundTrip(deep::ShipCompletedEvent{
        .orderId = deep::ShipyardOrderId{7},
        .colonyId = deep::ColonyId{8},
        .shipId = deep::ShipId{9},
        .fleetId = deep::FleetId{10},
        .shipClassId = deep::ShipClassId{11}
    }, "ship_completed payload round-trips");
}

void test_fleet_order_assigned_round_trips() {
    // Guards movement command persistence, especially the countdown field used
    // to resume in-flight orders after load.
    requireRoundTrip(deep::FleetOrderAssignedEvent{
        .fleetId = deep::FleetId{12},
        .originBodyId = deep::BodyId{13},
        .destinationBodyId = deep::BodyId{14},
        .daysRemaining = 15
    }, "fleet_order_assigned payload round-trips");
}

void test_fleet_arrived_round_trips() {
    // Protects the arrival audit payload that confirms movement cleanup reached
    // the intended destination body.
    requireRoundTrip(deep::FleetArrivedEvent{
        .fleetId = deep::FleetId{16},
        .destinationBodyId = deep::BodyId{17}
    }, "fleet_arrived payload round-trips");
}

void test_resource_survey_completed_round_trips() {
    // Resource surveys are durable exploration audit events. This protects
    // both confidence gains and a valid empty result during save serialization.
    requireRoundTrip(deep::ResourceSurveyCompletedEvent{
        .fleetId = deep::FleetId{18},
        .bodyId = deep::BodyId{19},
        .observationBatchId = deep::ObservationBatchId{1}
    }, "resource_survey_completed payload round-trips");
    requireRoundTrip(deep::ResourceSurveyCompletedEvent{
        .fleetId = deep::FleetId{18},
        .bodyId = deep::BodyId{19},
        .observationBatchId = deep::ObservationBatchId{1}
    }, "zero-information resource_survey_completed payload round-trips");
}

void test_survey_program_audit_round_trips() {
    requireRoundTrip(deep::SurveyProgramAuditEvent{
        .programId = deep::SurveyProgramId{21},
        .kind = deep::SurveyProgramAuditKind::VisitCompleted,
        .fleetId = deep::FleetId{4},
        .bodyId = deep::BodyId{7},
        .leaderId = deep::PersonId{3},
        .approach = deep::SurveyPlanningApproach::PriorityFirst,
        .charterRevision = 2,
        .passNumber = 1,
        .fuelAmount = 12.5,
        .detail = "Survey \"pass\" complete\\review\nnext"
    }, "survey_program_audit complete payload round-trips");
    requireRoundTrip(deep::SurveyProgramAuditEvent{
        .programId = deep::SurveyProgramId{22},
        .kind = deep::SurveyProgramAuditKind::Authorized,
        .fleetId = std::nullopt,
        .bodyId = std::nullopt,
        .leaderId = std::nullopt,
        .detail = "Waiting for assigned assets"
    }, "survey_program_audit optional identities round-trip");
}

void test_command_rejected_round_trips_escaped_reason() {
    // Covers quotes, backslashes, and control escapes so future parser changes do
    // not corrupt player-facing validation diagnostics.
    requireRoundTrip(deep::CommandRejectedEvent{
        .reason = "Rejected \"fleet\" order: path C:\\DeepSignal\\save\nretry\tnow"
    }, "command_rejected payload round-trips escaped string content");
}

void test_freight_program_audit_round_trips_and_rejects_malformed() {
    // P3B freight IDs have their own payload tag: freight #1 never decodes as
    // survey #1. All current fields and absent participants survive the boundary.
    requireRoundTrip(deep::FreightProgramAuditEvent{
        .programId = deep::FreightProgramId{1}, .kind = deep::FreightProgramAuditKind::Transfer,
        .fleetId = deep::FleetId{4}, .colonyId = deep::ColonyId{7}, .leaderId = deep::PersonId{3},
        .charterRevision = 2, .shipmentNumber = 3, .amount = 25.5,
        .detail = "Unload \"cargo\"\\receipt\nnext"
    }, "freight audit fields round-trip with their typed program identity");
    requireRoundTrip(deep::FreightProgramAuditEvent{
        .programId = deep::FreightProgramId{2}, .kind = deep::FreightProgramAuditKind::Authorized,
        .detail = "Waiting for named assets"
    }, "freight audit optional identities round-trip");
    const std::string valid = R"({"program_id":1,"kind":0,"fleet_id":0,"colony_id":0,"leader_id":0,"charter_revision":1,"shipment_number":0,"amount":0,"detail":"x"})";
    for (const auto& [before, after] : {
            std::pair{std::string{"\"kind\":0"}, std::string{"\"kind\":99"}},
            std::pair{std::string{"\"fleet_id\":0"}, std::string{"\"fleet_id\":-1"}},
            std::pair{std::string{"\"shipment_number\":0"}, std::string{"\"shipment_number\":0.5"}},
            std::pair{std::string{"\"amount\":0"}, std::string{"\"amount\":1e9999"}}
        }) {
        std::string malformed = valid;
        malformed.replace(malformed.find(before), before.size(), after);
        requireThrows("malformed freight audit fields reject", [&malformed] {
            (void)deep::save::eventPayloadFromJson("freight_program_audit", malformed);
        });
    }
    requireThrows("nonfinite freight transfer cannot be serialized", [] {
        (void)deep::save::eventPayloadToJson(deep::FreightProgramAuditEvent{
            .programId = deep::FreightProgramId{1}, .kind = deep::FreightProgramAuditKind::Transfer,
            .amount = std::numeric_limits<double>::infinity()
        });
    });
}

void test_unknown_event_type_is_rejected() {
    // Unknown persisted event names must fail loudly so schema migrations cannot
    // silently drop newly introduced event payloads.
    requireThrows("unknown event type is rejected", [] {
        static_cast<void>(deep::save::eventPayloadFromJson("unknown_event", "{}"));
    });
}

void test_missing_required_field_is_rejected() {
    // Missing required fields indicate corrupt or mismatched save data and must
    // not be replaced with default IDs or zero-valued quantities.
    requireThrows("missing required field is rejected", [] {
        static_cast<void>(deep::save::eventPayloadFromJson(
            "fleet_arrived",
            R"({"fleet_id":1})"));
    });
}

void test_invalid_mineral_ordinal_is_rejected() {
    // Mineral ordinals are persisted schema data. Rejecting out-of-range values
    // prevents corrupted saves from indexing outside MineralSet later.
    requireThrows("invalid mineral ordinal is rejected", [] {
        static_cast<void>(deep::save::eventPayloadFromJson(
            "mineral_extracted",
            R"({"colony_id":1,"body_id":2,"mineral":14,"amount":1.0,"remaining_deposit":2.0})"));
    });
}

void test_invalid_survey_program_audit_enum_is_rejected() {
    requireThrows("invalid survey program audit kind is rejected", [] {
        static_cast<void>(deep::save::eventPayloadFromJson(
            "survey_program_audit",
            R"({"program_id":1,"kind":99,"fleet_id":0,"body_id":0,"leader_id":0,"approach":0,"charter_revision":1,"pass_number":0,"fuel_amount":0,"detail":"x"})"));
    });
}

void test_non_finite_numeric_payload_is_rejected() {
    // A huge JSON number is valid syntax but cannot represent finite simulation
    // state. This protects both the fallback parser and nlohmann-backed path.
    requireThrows("non-finite numeric payload is rejected", [] {
        static_cast<void>(deep::save::eventPayloadFromJson(
            "mineral_extracted",
            R"({"colony_id":1,"body_id":2,"mineral":0,"amount":1e9999,"remaining_deposit":2.0})"));
    });
}

void test_non_finite_numeric_serialization_is_rejected() {
    // Serialization must reject non-finite event data before nlohmann/json can
    // encode it as null or the fallback writer can emit non-JSON tokens.
    requireThrows("NaN amount serialization is rejected", [] {
        static_cast<void>(deep::save::eventPayloadToJson(deep::MineralExtractedEvent{
            .colonyId = deep::ColonyId{1},
            .bodyId = deep::BodyId{2},
            .mineral = deep::Mineral::Iron,
            .amount = std::numeric_limits<double>::quiet_NaN(),
            .remainingDeposit = 2.0
        }));
    });

    requireThrows("infinite remaining deposit serialization is rejected", [] {
        static_cast<void>(deep::save::eventPayloadToJson(deep::MineralExtractedEvent{
            .colonyId = deep::ColonyId{1},
            .bodyId = deep::BodyId{2},
            .mineral = deep::Mineral::Iron,
            .amount = 1.0,
            .remainingDeposit = std::numeric_limits<double>::infinity()
        }));
    });
}

void test_integer_overflow_is_rejected() {
    // quantity and days_remaining narrow from persisted int64 values to int.
    // Overflow must fail at the save boundary instead of wrapping silently.
    const std::string overflowingInt = std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1LL);

    requireThrows("quantity overflow is rejected", [&overflowingInt] {
        static_cast<void>(deep::save::eventPayloadFromJson(
            "shipyard_order_created",
            "{\"order_id\":1,\"colony_id\":2,\"ship_class_id\":3,\"quantity\":" + overflowingInt + "}"));
    });

    requireThrows("days_remaining overflow is rejected", [&overflowingInt] {
        static_cast<void>(deep::save::eventPayloadFromJson(
            "fleet_order_assigned",
            "{\"fleet_id\":1,\"origin_body_id\":2,\"destination_body_id\":3,\"days_remaining\":" + overflowingInt + "}"));
    });
}

void test_event_type_names_are_stable_schema_v1_strings() {
    // These strings are stored in event_log.event_type. Renaming one requires a
    // save migration, so this test catches accidental churn.
    require(deep::save::eventTypeName(deep::MineralExtractedEvent{}) == "mineral_extracted",
            "mineral_extracted type name is stable");
    require(deep::save::eventTypeName(deep::ShipyardOrderCreatedEvent{}) == "shipyard_order_created",
            "shipyard_order_created type name is stable");
    require(deep::save::eventTypeName(deep::ShipClassRevisionCreatedEvent{}) == "ship_class_revision_created",
            "ship_class_revision_created type name is stable");
    require(deep::save::eventTypeName(deep::ShipCompletedEvent{}) == "ship_completed",
            "ship_completed type name is stable");
    require(deep::save::eventTypeName(deep::FleetOrderAssignedEvent{}) == "fleet_order_assigned",
            "fleet_order_assigned type name is stable");
    require(deep::save::eventTypeName(deep::FleetArrivedEvent{}) == "fleet_arrived",
            "fleet_arrived type name is stable");
    require(deep::save::eventTypeName(deep::ResourceSurveyCompletedEvent{}) == "resource_survey_completed",
            "resource_survey_completed type name is stable");
    require(deep::save::eventTypeName(deep::SurveyProgramAuditEvent{}) == "survey_program_audit",
            "survey_program_audit type name is stable");
    require(deep::save::eventTypeName(deep::CommandRejectedEvent{}) == "command_rejected",
            "command_rejected type name is stable");
}

void runAllTests() {
    // P3C operating work and service history use distinct typed envelopes.
    requireRoundTrip(deep::EquipmentDutyUsedEvent{deep::FleetId{1},deep::ShipId{2},deep::ShipComponentId{4},
        deep::SurveyProgramId{1},1.0,8.0,9.0}, "actual timed duty event round-trips");
    requireRoundTrip(deep::EquipmentDutyUsedEvent{deep::FleetId{1},deep::ShipId{2},deep::ShipComponentId{4},
        std::nullopt,5.0,0.0,5.0}, "manual duty event retains absent program identity");
    requireRoundTrip(deep::MaintenanceProgramAuditEvent{deep::MaintenanceProgramId{1},deep::MaintenanceAuditKind::WorkPerformed,2,
        "Engineering work at an actual colony"}, "maintenance audit and job identity round-trip");
    test_mineral_extracted_round_trips();
    test_shipyard_order_created_round_trips();
    test_ship_class_revision_created_round_trips();
    test_ship_completed_round_trips();
    test_fleet_order_assigned_round_trips();
    test_fleet_arrived_round_trips();
    test_resource_survey_completed_round_trips();
    test_survey_program_audit_round_trips();
    test_freight_program_audit_round_trips_and_rejects_malformed();
    test_command_rejected_round_trips_escaped_reason();
    test_unknown_event_type_is_rejected();
    test_missing_required_field_is_rejected();
    test_invalid_mineral_ordinal_is_rejected();
    test_invalid_survey_program_audit_enum_is_rejected();
    test_non_finite_numeric_payload_is_rejected();
    test_non_finite_numeric_serialization_is_rejected();
    test_integer_overflow_is_rejected();
    test_event_type_names_are_stable_schema_v1_strings();
}

} // namespace

int main() {
    try {
        runAllTests();
        std::cout << "Event JSON tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "Event JSON test failure: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}

#include "save/EventJson.h"
#include "sim/Events.h"
#include "sim/Minerals.h"

// Direct regression tests for schema v1 event JSON serialization.
// These tests intentionally exercise EventJson without SaveGameRepository so the
// payload grammar remains protected before any JSON dependency policy changes.

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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
        .mineral = deep::Mineral::Electronics,
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

void test_command_rejected_round_trips_escaped_reason() {
    // Covers quotes, backslashes, and control escapes so future parser changes do
    // not corrupt player-facing validation diagnostics.
    requireRoundTrip(deep::CommandRejectedEvent{
        .reason = "Rejected \"fleet\" order: path C:\\DeepSignal\\save\nretry\tnow"
    }, "command_rejected payload round-trips escaped string content");
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
            R"({"colony_id":1,"body_id":2,"mineral":5,"amount":1.0,"remaining_deposit":2.0})"));
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
    require(deep::save::eventTypeName(deep::ShipCompletedEvent{}) == "ship_completed",
            "ship_completed type name is stable");
    require(deep::save::eventTypeName(deep::FleetOrderAssignedEvent{}) == "fleet_order_assigned",
            "fleet_order_assigned type name is stable");
    require(deep::save::eventTypeName(deep::FleetArrivedEvent{}) == "fleet_arrived",
            "fleet_arrived type name is stable");
    require(deep::save::eventTypeName(deep::CommandRejectedEvent{}) == "command_rejected",
            "command_rejected type name is stable");
}

void runAllTests() {
    test_mineral_extracted_round_trips();
    test_shipyard_order_created_round_trips();
    test_ship_completed_round_trips();
    test_fleet_order_assigned_round_trips();
    test_fleet_arrived_round_trips();
    test_command_rejected_round_trips_escaped_reason();
    test_unknown_event_type_is_rejected();
    test_missing_required_field_is_rejected();
    test_invalid_mineral_ordinal_is_rejected();
    test_non_finite_numeric_payload_is_rejected();
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

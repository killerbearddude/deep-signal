#include "save/EventJson.h"

// Responsibility: encode/decode persisted event payload shapes in the active schema.
// This module does not own tables, transactions, or GameState reference checks.
// Header availability selects the JSON implementation at compilation time; the
// fallback implements only the compact flat format used by its writer and has
// different grammar/escaping behavior from nlohmann. Revisit this boundary when
// externally authored payloads or cross-build save portability are required.

#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#define DEEP_SIGNAL_HAS_NLOHMANN_JSON 1
#else
#define DEEP_SIGNAL_HAS_NLOHMANN_JSON 0
#endif

#include "sim/IdTypes.h"
#include "sim/Minerals.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>

namespace deep::save {
namespace {

// Converts strongly typed IDs to their persisted integer representation.
template <typename IdT>
[[nodiscard]] std::int64_t idValue(const IdT id) noexcept {
    return id.value;
}

// Converts enum values to stable persisted ordinals. Schema migrations must
// account for any future enum reordering.
template <typename EnumT>
[[nodiscard]] std::int64_t enumValue(const EnumT value) noexcept {
    return static_cast<std::int64_t>(value);
}

// Validates enum ordinals read from JSON payloads before reconstructing typed
// event records. Payloads are part of the untrusted save boundary.
template <typename EnumT>
[[nodiscard]] bool isValidEnumValue(const std::int64_t value) noexcept {
    if constexpr (std::is_same_v<EnumT, Mineral>) {
        return value >= 0 && value < static_cast<std::int64_t>(mineralCount());
    } else if constexpr (std::is_same_v<EnumT, SurveyPlanningApproach>) {
        return value >= 0 && value <= static_cast<std::int64_t>(SurveyPlanningApproach::PriorityFirst);
    } else if constexpr (std::is_same_v<EnumT, SurveyProgramAuditKind>) {
        return value >= 0 && value <= static_cast<std::int64_t>(SurveyProgramAuditKind::IssueAcknowledged);
    } else if constexpr (std::is_same_v<EnumT, FreightProgramAuditKind>) {
        return value >= 0 && value <= static_cast<std::int64_t>(FreightProgramAuditKind::IssueRaised);
    } else if constexpr (std::is_same_v<EnumT, AnalysisAuditKind>) {
        return value >= 0 && value <= static_cast<std::int64_t>(AnalysisAuditKind::IssueAcknowledged);
    } else if constexpr (std::is_same_v<EnumT, MaintenanceAuditKind>) {
        return value >= 0 && value <= static_cast<std::int64_t>(MaintenanceAuditKind::IssueAcknowledged);
    } else {
        static_assert(std::is_enum_v<EnumT>, "enumFromValue requires an enum type");
        return false;
    }
}

template <typename EnumT>
[[nodiscard]] EnumT enumFromValue(const std::int64_t value) {
    if (!isValidEnumValue<EnumT>(value)) {
        throw std::runtime_error{"Save event payload contains an invalid enum value"};
    }
    return static_cast<EnumT>(value);
}

// Converts JSON integer payload values to int after checking range. This mirrors
// the repository's SQL integer narrowing guard for event-specific fields.
[[nodiscard]] int checkedIntFromPayload(const std::int64_t value, const std::string_view fieldName) {
    if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error{std::string{"Save event integer is out of range for field: "} + std::string{fieldName}};
    }
    return static_cast<int>(value);
}

// Event payload JSON is persisted audit data. Reject non-finite doubles before
// either serializer can turn them into implementation-specific text or nulls.
[[nodiscard]] double checkedFiniteDoubleFromPayload(const double value, const std::string_view fieldName) {
    if (!std::isfinite(value)) {
        throw std::runtime_error{std::string{"Save event double is non-finite for field: "} + std::string{fieldName}};
    }
    return value;
}

// Fallback-only helpers are compiled out when nlohmann/json is available.
// The local decoder keeps dependency-free builds possible, but must not be
// treated as a general-purpose or equivalently strict JSON parser.
#if !DEEP_SIGNAL_HAS_NLOHMANN_JSON

// Writes finite doubles with round-trip precision. Assumes the stream's locale
// uses JSON-compatible numeric punctuation; no locale is explicitly imbued here.
[[nodiscard]] std::string numberToJson(const double value, const std::string_view fieldName) {
    const double checkedValue = checkedFiniteDoubleFromPayload(value, fieldName);
    std::ostringstream out;
    out << std::setprecision(17) << checkedValue;
    return out.str();
}

// Escapes quotes, backslashes, newline, carriage return, and tab, leaving UTF-8
// bytes intact. Other control characters are not escaped, so arbitrary strings
// are outside this fallback writer's safe output contract.
[[nodiscard]] std::string escapeJsonString(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (const char ch : value) {
        switch (ch) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

// Decodes only the escape subset emitted by this writer. Valid JSON escapes such
// as Unicode, backspace, and form feed are unsupported and throw; this limitation
// matters when reading payloads written by another JSON implementation.
[[nodiscard]] std::string unescapeJsonString(const std::string_view value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') {
            unescaped += value[i];
            continue;
        }

        if (i + 1 >= value.size()) {
            throw std::runtime_error{"Malformed JSON escape in save payload"};
        }

        ++i;
        switch (value[i]) {
        case '"':
            unescaped += '"';
            break;
        case '\\':
            unescaped += '\\';
            break;
        case 'n':
            unescaped += '\n';
            break;
        case 'r':
            unescaped += '\r';
            break;
        case 't':
            unescaped += '\t';
            break;
        default:
            throw std::runtime_error{"Unsupported JSON escape in save payload"};
        }
    }
    return unescaped;
}

// Checks only the outer braces, without accepting surrounding whitespace or
// validating the interior grammar. Per-field extraction adds limited checks.
void requireFlatJsonObjectShape(const std::string_view json) {
    if (json.size() < 2 || json.front() != '{' || json.back() != '}') {
        throw std::runtime_error{"Malformed event payload JSON object"};
    }
}

// Extracts a field by its exact compact spelling, not by parsing object members.
// Assumes no whitespace around the colon and no ambiguous matching substrings.
// Quoted and unquoted values share the same return type, so this helper cannot
// enforce JSON field types, duplicate-key policy, or full structural validity.
[[nodiscard]] std::string jsonFieldRaw(const std::string_view json, const std::string_view key) {
    requireFlatJsonObjectShape(json);

    const std::string needle = '"' + std::string{key} + "\":";
    const std::size_t keyPos = json.find(needle);
    if (keyPos == std::string_view::npos) {
        throw std::runtime_error{"Missing field in event payload JSON"};
    }

    std::size_t valuePos = keyPos + needle.size();
    if (valuePos >= json.size()) {
        throw std::runtime_error{"Malformed event payload JSON"};
    }

    if (json[valuePos] == '"') {
        ++valuePos;
        std::string raw;
        bool escaped = false;
        for (; valuePos < json.size(); ++valuePos) {
            const char ch = json[valuePos];
            if (!escaped && ch == '"') {
                return raw;
            }
            if (!escaped && ch == '\\') {
                escaped = true;
                raw += ch;
                continue;
            }
            escaped = false;
            raw += ch;
        }
        throw std::runtime_error{"Unterminated JSON string in event payload"};
    }

    const std::size_t end = json.find_first_of(",}", valuePos);
    if (end == std::string_view::npos) {
        throw std::runtime_error{"Malformed event payload JSON value"};
    }
    return std::string{json.substr(valuePos, end - valuePos)};
}

[[nodiscard]] std::int64_t jsonInt64(const std::string_view json, const std::string_view key) {
    const std::string raw = jsonFieldRaw(json, key);
    std::int64_t value = 0;
    const auto* begin = raw.data();
    const auto* end = raw.data() + raw.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        throw std::runtime_error{"Invalid integer in event payload JSON"};
    }
    return value;
}

[[nodiscard]] double jsonDouble(const std::string_view json, const std::string_view key) {
    const std::string raw = jsonFieldRaw(json, key);
    double value = 0.0;
    const auto* begin = raw.data();
    const auto* end = raw.data() + raw.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || !std::isfinite(value)) {
        throw std::runtime_error{"Invalid double in event payload JSON"};
    }
    return value;
}

[[nodiscard]] std::string jsonString(const std::string_view json, const std::string_view key) {
    return unescapeJsonString(jsonFieldRaw(json, key));
}

[[nodiscard]] std::string quoteJson(const std::string_view value) {
    return '"' + escapeJsonString(value) + '"';
}

#endif // !DEEP_SIGNAL_HAS_NLOHMANN_JSON

} // namespace

[[nodiscard]] std::string eventTypeName(const SimEventPayload& payload) {
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;
        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            return "mineral_extracted";
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            return "shipyard_order_created";
        } else if constexpr (std::is_same_v<Event, ShipClassRevisionCreatedEvent>) {
            return "ship_class_revision_created";
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            return "ship_completed";
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            return "fleet_order_assigned";
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            return "fleet_arrived";
        } else if constexpr (std::is_same_v<Event, ResourceSurveyCompletedEvent>) {
            return "resource_survey_completed";
        } else if constexpr (std::is_same_v<Event, SurveyProgramAuditEvent>) {
            return "survey_program_audit";
        } else if constexpr (std::is_same_v<Event, FreightProgramAuditEvent>) {
            return "freight_program_audit";
        } else if constexpr (std::is_same_v<Event, EquipmentDutyUsedEvent>) {
            return "equipment_duty_used";
        } else if constexpr (std::is_same_v<Event, MaintenanceProgramAuditEvent>) {
            return "maintenance_program_audit";
        } else if constexpr (std::is_same_v<Event, AnalysisProgramAuditEvent>) {
            return "analysis_program_audit";
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            return "command_rejected";
        }
    }, payload);
}

[[nodiscard]] std::string eventPayloadToJson(const SimEventPayload& payload) {
#if DEEP_SIGNAL_HAS_NLOHMANN_JSON
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;
        nlohmann::json object = nlohmann::json::object();

        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            object["colony_id"] = idValue(event.colonyId);
            object["body_id"] = idValue(event.bodyId);
            object["mineral"] = enumValue(event.mineral);
            object["amount"] = checkedFiniteDoubleFromPayload(event.amount, "event.amount");
            object["remaining_deposit"] = checkedFiniteDoubleFromPayload(event.remainingDeposit, "event.remaining_deposit");
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            object["order_id"] = idValue(event.orderId);
            object["colony_id"] = idValue(event.colonyId);
            object["ship_class_id"] = idValue(event.shipClassId);
            object["quantity"] = event.quantity;
        } else if constexpr (std::is_same_v<Event, ShipClassRevisionCreatedEvent>) {
            object["ship_class_id"] = idValue(event.shipClassId);
            object["base_class_id"] = event.basedOnClassId.has_value() ? idValue(*event.basedOnClassId) : 0;
            object["revision"] = event.revision;
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            object["order_id"] = idValue(event.orderId);
            object["colony_id"] = idValue(event.colonyId);
            object["ship_id"] = idValue(event.shipId);
            object["fleet_id"] = idValue(event.fleetId);
            object["ship_class_id"] = idValue(event.shipClassId);
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            object["fleet_id"] = idValue(event.fleetId);
            object["origin_body_id"] = idValue(event.originBodyId);
            object["destination_body_id"] = idValue(event.destinationBodyId);
            object["days_remaining"] = event.daysRemaining;
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            object["fleet_id"] = idValue(event.fleetId);
            object["destination_body_id"] = idValue(event.destinationBodyId);
        } else if constexpr (std::is_same_v<Event, ResourceSurveyCompletedEvent>) {
            object["fleet_id"] = idValue(event.fleetId);
            object["body_id"] = idValue(event.bodyId);
            object["observation_batch_id"] = idValue(event.observationBatchId);
        } else if constexpr (std::is_same_v<Event, SurveyProgramAuditEvent>) {
            object["program_id"] = idValue(event.programId);
            object["kind"] = enumValue(event.kind);
            object["fleet_id"] = event.fleetId ? idValue(*event.fleetId) : 0;
            object["body_id"] = event.bodyId ? idValue(*event.bodyId) : 0;
            object["leader_id"] = event.leaderId ? idValue(*event.leaderId) : 0;
            object["approach"] = enumValue(event.approach);
            object["charter_revision"] = event.charterRevision;
            object["pass_number"] = event.passNumber;
            object["fuel_amount"] = checkedFiniteDoubleFromPayload(event.fuelAmount, "event.fuel_amount");
            object["detail"] = event.detail;
        } else if constexpr (std::is_same_v<Event, FreightProgramAuditEvent>) {
            object["program_id"] = idValue(event.programId);
            object["kind"] = enumValue(event.kind);
            object["fleet_id"] = event.fleetId ? idValue(*event.fleetId) : 0;
            object["colony_id"] = event.colonyId ? idValue(*event.colonyId) : 0;
            object["leader_id"] = event.leaderId ? idValue(*event.leaderId) : 0;
            object["charter_revision"] = event.charterRevision;
            object["shipment_number"] = event.shipmentNumber;
            object["amount"] = checkedFiniteDoubleFromPayload(event.amount, "event.amount");
            object["detail"] = event.detail;
        } else if constexpr (std::is_same_v<Event, EquipmentDutyUsedEvent>) {
            object["fleet_id"] = idValue(event.fleetId); object["ship_id"] = idValue(event.shipId);
            object["component_id"] = idValue(event.componentId);
            object["survey_program_id"] = event.surveyProgramId ? idValue(*event.surveyProgramId) : 0;
            object["duty"] = checkedFiniteDoubleFromPayload(event.duty,"event.duty");
            object["before_duty"] = checkedFiniteDoubleFromPayload(event.beforeUsedDuty,"event.before_duty");
            object["after_duty"] = checkedFiniteDoubleFromPayload(event.afterUsedDuty,"event.after_duty");
        } else if constexpr (std::is_same_v<Event, MaintenanceProgramAuditEvent>) {
            object["program_id"] = idValue(event.programId); object["kind"] = enumValue(event.kind);
            object["job_number"] = event.jobNumber; object["detail"] = event.detail;
        } else if constexpr (std::is_same_v<Event, AnalysisProgramAuditEvent>) {
            object["program_id"] = idValue(event.programId); object["kind"] = enumValue(event.kind);
            object["job_id"] = event.jobId.value; object["detail"] = event.detail;
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            object["reason"] = event.reason;
        }

        return object.dump();
    }, payload);
#else
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;
        std::ostringstream out;
        out << '{';

        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            out << "\"colony_id\":" << idValue(event.colonyId)
                << ",\"body_id\":" << idValue(event.bodyId)
                << ",\"mineral\":" << enumValue(event.mineral)
                << ",\"amount\":" << numberToJson(event.amount, "event.amount")
                << ",\"remaining_deposit\":" << numberToJson(event.remainingDeposit, "event.remaining_deposit");
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            out << "\"order_id\":" << idValue(event.orderId)
                << ",\"colony_id\":" << idValue(event.colonyId)
                << ",\"ship_class_id\":" << idValue(event.shipClassId)
                << ",\"quantity\":" << event.quantity;
        } else if constexpr (std::is_same_v<Event, ShipClassRevisionCreatedEvent>) {
            out << "\"ship_class_id\":" << idValue(event.shipClassId)
                << ",\"base_class_id\":" << (event.basedOnClassId.has_value() ? idValue(*event.basedOnClassId) : 0)
                << ",\"revision\":" << event.revision;
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            out << "\"order_id\":" << idValue(event.orderId)
                << ",\"colony_id\":" << idValue(event.colonyId)
                << ",\"ship_id\":" << idValue(event.shipId)
                << ",\"fleet_id\":" << idValue(event.fleetId)
                << ",\"ship_class_id\":" << idValue(event.shipClassId);
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            out << "\"fleet_id\":" << idValue(event.fleetId)
                << ",\"origin_body_id\":" << idValue(event.originBodyId)
                << ",\"destination_body_id\":" << idValue(event.destinationBodyId)
                << ",\"days_remaining\":" << event.daysRemaining;
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            out << "\"fleet_id\":" << idValue(event.fleetId)
                << ",\"destination_body_id\":" << idValue(event.destinationBodyId);
        } else if constexpr (std::is_same_v<Event, ResourceSurveyCompletedEvent>) {
            out << "\"fleet_id\":" << idValue(event.fleetId)
                << ",\"body_id\":" << idValue(event.bodyId)
                << ",\"observation_batch_id\":" << idValue(event.observationBatchId);
        } else if constexpr (std::is_same_v<Event, SurveyProgramAuditEvent>) {
            out << "\"program_id\":" << idValue(event.programId)
                << ",\"kind\":" << enumValue(event.kind)
                << ",\"fleet_id\":" << (event.fleetId ? idValue(*event.fleetId) : 0)
                << ",\"body_id\":" << (event.bodyId ? idValue(*event.bodyId) : 0)
                << ",\"leader_id\":" << (event.leaderId ? idValue(*event.leaderId) : 0)
                << ",\"approach\":" << enumValue(event.approach)
                << ",\"charter_revision\":" << event.charterRevision
                << ",\"pass_number\":" << event.passNumber
                << ",\"fuel_amount\":" << numberToJson(event.fuelAmount, "event.fuel_amount")
                << ",\"detail\":" << quoteJson(event.detail);
        } else if constexpr (std::is_same_v<Event, FreightProgramAuditEvent>) {
            out << "\"program_id\":" << idValue(event.programId)
                << ",\"kind\":" << enumValue(event.kind)
                << ",\"fleet_id\":" << (event.fleetId ? idValue(*event.fleetId) : 0)
                << ",\"colony_id\":" << (event.colonyId ? idValue(*event.colonyId) : 0)
                << ",\"leader_id\":" << (event.leaderId ? idValue(*event.leaderId) : 0)
                << ",\"charter_revision\":" << event.charterRevision
                << ",\"shipment_number\":" << event.shipmentNumber
                << ",\"amount\":" << numberToJson(event.amount, "event.amount")
                << ",\"detail\":" << quoteJson(event.detail);
        } else if constexpr (std::is_same_v<Event, EquipmentDutyUsedEvent>) {
            out << "\"fleet_id\":" << idValue(event.fleetId) << ",\"ship_id\":" << idValue(event.shipId)
                << ",\"component_id\":" << idValue(event.componentId)
                << ",\"survey_program_id\":" << (event.surveyProgramId ? idValue(*event.surveyProgramId) : 0)
                << ",\"duty\":" << numberToJson(event.duty,"event.duty")
                << ",\"before_duty\":" << numberToJson(event.beforeUsedDuty,"event.before_duty")
                << ",\"after_duty\":" << numberToJson(event.afterUsedDuty,"event.after_duty");
        } else if constexpr (std::is_same_v<Event, MaintenanceProgramAuditEvent>) {
            out << "\"program_id\":" << idValue(event.programId) << ",\"kind\":" << enumValue(event.kind)
                << ",\"job_number\":" << event.jobNumber << ",\"detail\":" << quoteJson(event.detail);
        } else if constexpr (std::is_same_v<Event, AnalysisProgramAuditEvent>) {
            out << "\"program_id\":" << idValue(event.programId) << ",\"kind\":" << enumValue(event.kind)
                << ",\"job_id\":" << event.jobId.value << ",\"detail\":" << quoteJson(event.detail);
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            out << "\"reason\":" << quoteJson(event.reason);
        }

        out << '}';
        return out.str();
    }, payload);
#endif
}

[[nodiscard]] SimEventPayload eventPayloadFromJson(const std::string_view eventType, const std::string_view payloadJson) {
#if DEEP_SIGNAL_HAS_NLOHMANN_JSON
    const nlohmann::json object = nlohmann::json::parse(payloadJson.begin(), payloadJson.end());
    if (!object.is_object()) {
        throw std::runtime_error{"Malformed event payload JSON object"};
    }

    const auto requireInt64 = [&object](const std::string_view key) -> std::int64_t {
        const auto it = object.find(std::string{key});
        if (it == object.end() || !it->is_number_integer()) {
            throw std::runtime_error{"Missing or invalid integer in event payload JSON"};
        }
        return it->get<std::int64_t>();
    };

    const auto requireDouble = [&object](const std::string_view key) -> double {
        const auto it = object.find(std::string{key});
        if (it == object.end() || !it->is_number()) {
            throw std::runtime_error{"Missing or invalid number in event payload JSON"};
        }
        const double value = it->get<double>();
        if (!std::isfinite(value)) {
            throw std::runtime_error{"Invalid double in event payload JSON"};
        }
        return value;
    };

    const auto requireString = [&object](const std::string_view key) -> std::string {
        const auto it = object.find(std::string{key});
        if (it == object.end() || !it->is_string()) {
            throw std::runtime_error{"Missing or invalid string in event payload JSON"};
        }
        return it->get<std::string>();
    };

    if (eventType == "mineral_extracted") {
        return MineralExtractedEvent{
            .colonyId = ColonyId{requireInt64("colony_id")},
            .bodyId = BodyId{requireInt64("body_id")},
            .mineral = enumFromValue<Mineral>(requireInt64("mineral")),
            .amount = requireDouble("amount"),
            .remainingDeposit = requireDouble("remaining_deposit")
        };
    }

    if (eventType == "shipyard_order_created") {
        return ShipyardOrderCreatedEvent{
            .orderId = ShipyardOrderId{requireInt64("order_id")},
            .colonyId = ColonyId{requireInt64("colony_id")},
            .shipClassId = ShipClassId{requireInt64("ship_class_id")},
            .quantity = checkedIntFromPayload(requireInt64("quantity"), "event.quantity")
        };
    }

    if (eventType == "ship_class_revision_created") {
        const std::int64_t base = requireInt64("base_class_id");
        return ShipClassRevisionCreatedEvent{
            .shipClassId = ShipClassId{requireInt64("ship_class_id")},
            .basedOnClassId = base == 0 ? std::nullopt : std::optional{ShipClassId{base}},
            .revision = checkedIntFromPayload(requireInt64("revision"), "event.revision")
        };
    }

    if (eventType == "ship_completed") {
        return ShipCompletedEvent{
            .orderId = ShipyardOrderId{requireInt64("order_id")},
            .colonyId = ColonyId{requireInt64("colony_id")},
            .shipId = ShipId{requireInt64("ship_id")},
            .fleetId = FleetId{requireInt64("fleet_id")},
            .shipClassId = ShipClassId{requireInt64("ship_class_id")}
        };
    }

    if (eventType == "fleet_order_assigned") {
        return FleetOrderAssignedEvent{
            .fleetId = FleetId{requireInt64("fleet_id")},
            .originBodyId = BodyId{requireInt64("origin_body_id")},
            .destinationBodyId = BodyId{requireInt64("destination_body_id")},
            .daysRemaining = checkedIntFromPayload(requireInt64("days_remaining"), "event.days_remaining")
        };
    }

    if (eventType == "fleet_arrived") {
        return FleetArrivedEvent{
            .fleetId = FleetId{requireInt64("fleet_id")},
            .destinationBodyId = BodyId{requireInt64("destination_body_id")}
        };
    }

    if (eventType == "resource_survey_completed") {
        return ResourceSurveyCompletedEvent{
            .fleetId = FleetId{requireInt64("fleet_id")},
            .bodyId = BodyId{requireInt64("body_id")},
            .observationBatchId = ObservationBatchId{requireInt64("observation_batch_id")}
        };
    }

    if (eventType == "survey_program_audit") {
        const std::int64_t fleetId = requireInt64("fleet_id");
        const std::int64_t bodyId = requireInt64("body_id");
        const std::int64_t leaderId = requireInt64("leader_id");
        if (fleetId < 0 || bodyId < 0 || leaderId < 0) {
            throw std::runtime_error{"Invalid optional ID in survey program audit payload"};
        }
        return SurveyProgramAuditEvent{
            .programId = SurveyProgramId{requireInt64("program_id")},
            .kind = enumFromValue<SurveyProgramAuditKind>(requireInt64("kind")),
            .fleetId = fleetId == 0 ? std::nullopt : std::optional{FleetId{fleetId}},
            .bodyId = bodyId == 0 ? std::nullopt : std::optional{BodyId{bodyId}},
            .leaderId = leaderId == 0 ? std::nullopt : std::optional{PersonId{leaderId}},
            .approach = enumFromValue<SurveyPlanningApproach>(requireInt64("approach")),
            .charterRevision = checkedIntFromPayload(requireInt64("charter_revision"), "event.charter_revision"),
            .passNumber = checkedIntFromPayload(requireInt64("pass_number"), "event.pass_number"),
            .fuelAmount = requireDouble("fuel_amount"),
            .detail = requireString("detail")
        };
    }

    if (eventType == "freight_program_audit") {
        const std::int64_t fleetId = requireInt64("fleet_id");
        const std::int64_t colonyId = requireInt64("colony_id");
        const std::int64_t leaderId = requireInt64("leader_id");
        if (fleetId < 0 || colonyId < 0 || leaderId < 0) {
            throw std::runtime_error{"Invalid optional ID in freight program audit payload"};
        }
        return FreightProgramAuditEvent{
            .programId = FreightProgramId{requireInt64("program_id")},
            .kind = enumFromValue<FreightProgramAuditKind>(requireInt64("kind")),
            .fleetId = fleetId == 0 ? std::nullopt : std::optional{FleetId{fleetId}},
            .colonyId = colonyId == 0 ? std::nullopt : std::optional{ColonyId{colonyId}},
            .leaderId = leaderId == 0 ? std::nullopt : std::optional{PersonId{leaderId}},
            .charterRevision = checkedIntFromPayload(requireInt64("charter_revision"), "event.charter_revision"),
            .shipmentNumber = checkedIntFromPayload(requireInt64("shipment_number"), "event.shipment_number"),
            .amount = requireDouble("amount"),
            .detail = requireString("detail")
        };
    }

    if (eventType == "equipment_duty_used") {
        const auto id = requireInt64("survey_program_id");
        if (id < 0) throw std::runtime_error("Invalid optional survey ID in duty event");
        return EquipmentDutyUsedEvent{FleetId{requireInt64("fleet_id")},ShipId{requireInt64("ship_id")},
            ShipComponentId{requireInt64("component_id")},id==0?std::nullopt:std::optional{SurveyProgramId{id}},
            requireDouble("duty"),requireDouble("before_duty"),requireDouble("after_duty")};
    }
    if (eventType == "maintenance_program_audit") {
        return MaintenanceProgramAuditEvent{MaintenanceProgramId{requireInt64("program_id")},
            enumFromValue<MaintenanceAuditKind>(requireInt64("kind")),
            checkedIntFromPayload(requireInt64("job_number"),"event.job_number"),requireString("detail")};
    }
    if (eventType == "analysis_program_audit") {
        return AnalysisProgramAuditEvent{AnalysisProgramId{requireInt64("program_id")},
            enumFromValue<AnalysisAuditKind>(requireInt64("kind")),
            AnalysisJobId{requireInt64("job_id")},requireString("detail")};
    }
    if (eventType == "command_rejected") {
        return CommandRejectedEvent{.reason = requireString("reason")};
    }

    throw std::runtime_error{"Unknown event type in save file"};
#else

    if (eventType == "mineral_extracted") {
        return MineralExtractedEvent{
            .colonyId = ColonyId{jsonInt64(payloadJson, "colony_id")},
            .bodyId = BodyId{jsonInt64(payloadJson, "body_id")},
            .mineral = enumFromValue<Mineral>(jsonInt64(payloadJson, "mineral")),
            .amount = jsonDouble(payloadJson, "amount"),
            .remainingDeposit = jsonDouble(payloadJson, "remaining_deposit")
        };
    }

    if (eventType == "shipyard_order_created") {
        return ShipyardOrderCreatedEvent{
            .orderId = ShipyardOrderId{jsonInt64(payloadJson, "order_id")},
            .colonyId = ColonyId{jsonInt64(payloadJson, "colony_id")},
            .shipClassId = ShipClassId{jsonInt64(payloadJson, "ship_class_id")},
            .quantity = checkedIntFromPayload(jsonInt64(payloadJson, "quantity"), "event.quantity")
        };
    }

    if (eventType == "ship_class_revision_created") {
        const std::int64_t base = jsonInt64(payloadJson, "base_class_id");
        return ShipClassRevisionCreatedEvent{
            .shipClassId = ShipClassId{jsonInt64(payloadJson, "ship_class_id")},
            .basedOnClassId = base == 0 ? std::nullopt : std::optional{ShipClassId{base}},
            .revision = checkedIntFromPayload(jsonInt64(payloadJson, "revision"), "event.revision")
        };
    }

    if (eventType == "ship_completed") {
        return ShipCompletedEvent{
            .orderId = ShipyardOrderId{jsonInt64(payloadJson, "order_id")},
            .colonyId = ColonyId{jsonInt64(payloadJson, "colony_id")},
            .shipId = ShipId{jsonInt64(payloadJson, "ship_id")},
            .fleetId = FleetId{jsonInt64(payloadJson, "fleet_id")},
            .shipClassId = ShipClassId{jsonInt64(payloadJson, "ship_class_id")}
        };
    }

    if (eventType == "fleet_order_assigned") {
        return FleetOrderAssignedEvent{
            .fleetId = FleetId{jsonInt64(payloadJson, "fleet_id")},
            .originBodyId = BodyId{jsonInt64(payloadJson, "origin_body_id")},
            .destinationBodyId = BodyId{jsonInt64(payloadJson, "destination_body_id")},
            .daysRemaining = checkedIntFromPayload(jsonInt64(payloadJson, "days_remaining"), "event.days_remaining")
        };
    }

    if (eventType == "fleet_arrived") {
        return FleetArrivedEvent{
            .fleetId = FleetId{jsonInt64(payloadJson, "fleet_id")},
            .destinationBodyId = BodyId{jsonInt64(payloadJson, "destination_body_id")}
        };
    }

    if (eventType == "resource_survey_completed") {
        return ResourceSurveyCompletedEvent{
            .fleetId = FleetId{jsonInt64(payloadJson, "fleet_id")},
            .bodyId = BodyId{jsonInt64(payloadJson, "body_id")},
            .observationBatchId = ObservationBatchId{jsonInt64(payloadJson, "observation_batch_id")}
        };
    }

    if (eventType == "survey_program_audit") {
        const std::int64_t fleetId = jsonInt64(payloadJson, "fleet_id");
        const std::int64_t bodyId = jsonInt64(payloadJson, "body_id");
        const std::int64_t leaderId = jsonInt64(payloadJson, "leader_id");
        if (fleetId < 0 || bodyId < 0 || leaderId < 0) {
            throw std::runtime_error{"Invalid optional ID in survey program audit payload"};
        }
        return SurveyProgramAuditEvent{
            .programId = SurveyProgramId{jsonInt64(payloadJson, "program_id")},
            .kind = enumFromValue<SurveyProgramAuditKind>(jsonInt64(payloadJson, "kind")),
            .fleetId = fleetId == 0 ? std::nullopt : std::optional{FleetId{fleetId}},
            .bodyId = bodyId == 0 ? std::nullopt : std::optional{BodyId{bodyId}},
            .leaderId = leaderId == 0 ? std::nullopt : std::optional{PersonId{leaderId}},
            .approach = enumFromValue<SurveyPlanningApproach>(jsonInt64(payloadJson, "approach")),
            .charterRevision = checkedIntFromPayload(jsonInt64(payloadJson, "charter_revision"), "event.charter_revision"),
            .passNumber = checkedIntFromPayload(jsonInt64(payloadJson, "pass_number"), "event.pass_number"),
            .fuelAmount = jsonDouble(payloadJson, "fuel_amount"),
            .detail = jsonString(payloadJson, "detail")
        };
    }

    if (eventType == "freight_program_audit") {
        const std::int64_t fleetId = jsonInt64(payloadJson, "fleet_id");
        const std::int64_t colonyId = jsonInt64(payloadJson, "colony_id");
        const std::int64_t leaderId = jsonInt64(payloadJson, "leader_id");
        if (fleetId < 0 || colonyId < 0 || leaderId < 0) {
            throw std::runtime_error{"Invalid optional ID in freight program audit payload"};
        }
        return FreightProgramAuditEvent{
            .programId = FreightProgramId{jsonInt64(payloadJson, "program_id")},
            .kind = enumFromValue<FreightProgramAuditKind>(jsonInt64(payloadJson, "kind")),
            .fleetId = fleetId == 0 ? std::nullopt : std::optional{FleetId{fleetId}},
            .colonyId = colonyId == 0 ? std::nullopt : std::optional{ColonyId{colonyId}},
            .leaderId = leaderId == 0 ? std::nullopt : std::optional{PersonId{leaderId}},
            .charterRevision = checkedIntFromPayload(jsonInt64(payloadJson, "charter_revision"), "event.charter_revision"),
            .shipmentNumber = checkedIntFromPayload(jsonInt64(payloadJson, "shipment_number"), "event.shipment_number"),
            .amount = jsonDouble(payloadJson, "amount"),
            .detail = jsonString(payloadJson, "detail")
        };
    }

    if (eventType == "equipment_duty_used") {
        const auto id = jsonInt64(payloadJson,"survey_program_id");
        if (id < 0) throw std::runtime_error("Invalid optional survey ID in duty event");
        return EquipmentDutyUsedEvent{FleetId{jsonInt64(payloadJson,"fleet_id")},ShipId{jsonInt64(payloadJson,"ship_id")},
            ShipComponentId{jsonInt64(payloadJson,"component_id")},id==0?std::nullopt:std::optional{SurveyProgramId{id}},
            jsonDouble(payloadJson,"duty"),jsonDouble(payloadJson,"before_duty"),jsonDouble(payloadJson,"after_duty")};
    }
    if (eventType == "maintenance_program_audit") {
        return MaintenanceProgramAuditEvent{MaintenanceProgramId{jsonInt64(payloadJson,"program_id")},
            enumFromValue<MaintenanceAuditKind>(jsonInt64(payloadJson,"kind")),
            checkedIntFromPayload(jsonInt64(payloadJson,"job_number"),"event.job_number"),jsonString(payloadJson,"detail")};
    }
    if (eventType == "analysis_program_audit") {
        return AnalysisProgramAuditEvent{AnalysisProgramId{jsonInt64(payloadJson,"program_id")},
            enumFromValue<AnalysisAuditKind>(jsonInt64(payloadJson,"kind")),
            AnalysisJobId{jsonInt64(payloadJson,"job_id")},jsonString(payloadJson,"detail")};
    }
    if (eventType == "command_rejected") {
        return CommandRejectedEvent{.reason = jsonString(payloadJson, "reason")};
    }

    throw std::runtime_error{"Unknown event type in save file"};

#endif
}



} // namespace deep::save

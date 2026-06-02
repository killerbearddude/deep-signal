#include "app/SimulationQueries.h"

// Implements read-only query projection from GameState into app-layer DTOs.
// The functions in this file are intentionally presentation-adjacent but UI-free:
// they resolve names and flatten variants without depending on ImGui or SDL.

#include "sim/GameState.h"
#include "sim/Minerals.h"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace deep {
namespace {

// Finds a read-only record by ID for name resolution. The returned pointer is
// non-owning and never escapes the query function that requested it.
template <typename T, typename IdT>
[[nodiscard]] const T* findById(const std::vector<T>& items, const IdT id) noexcept {
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) {
        return item.id == id;
    });
    return it == items.end() ? nullptr : &(*it);
}

[[nodiscard]] std::string bodyName(const GameState& state, const BodyId id) {
    const Body* body = findById(state.bodies, id);
    return body == nullptr ? std::string{"<unknown body>"} : body->name;
}

[[nodiscard]] const Body* bodyById(const GameState& state, const BodyId id) noexcept {
    return findById(state.bodies, id);
}

[[nodiscard]] std::string bodyTypeName(const BodyType type) {
    switch (type) {
    case BodyType::Star:
        return "Star";
    case BodyType::Terrestrial:
        return "Terrestrial";
    case BodyType::GasGiant:
        return "Gas Giant";
    case BodyType::Moon:
        return "Moon";
    case BodyType::Asteroid:
        return "Asteroid";
    }

    return "Unknown";
}

[[nodiscard]] std::string colonyName(const GameState& state, const ColonyId id) {
    const Colony* colony = findById(state.colonies, id);
    return colony == nullptr ? std::string{"<unknown colony>"} : colony->name;
}

[[nodiscard]] std::string shipClassName(const GameState& state, const ShipClassId id) {
    const ShipClass* shipClass = findById(state.shipClasses, id);
    return shipClass == nullptr ? std::string{"<unknown ship class>"} : shipClass->name;
}

[[nodiscard]] std::string shipRoleName(const ShipRole role) {
    switch (role) {
    case ShipRole::Survey:
        return "Survey";
    case ShipRole::Freighter:
        return "Freighter";
    case ShipRole::Escort:
        return "Escort";
    }

    return "Unknown";
}

[[nodiscard]] double shipClassBuildPoints(const GameState& state, const ShipClassId id) noexcept {
    const ShipClass* shipClass = findById(state.shipClasses, id);
    return shipClass == nullptr ? 0.0 : shipClass->buildPoints;
}

[[nodiscard]] std::string statusName(const ShipyardOrderStatus status) {
    switch (status) {
    case ShipyardOrderStatus::Active:
        return "Active";
    case ShipyardOrderStatus::Completed:
        return "Completed";
    }

    return "Unknown";
}

[[nodiscard]] std::string fleetOrderName(const FleetOrderType type) {
    switch (type) {
    case FleetOrderType::None:
        return "None";
    case FleetOrderType::MoveToBody:
        return "MoveToBody";
    }

    return "Unknown";
}

[[nodiscard]] std::string severityName(const EventSeverity severity) {
    switch (severity) {
    case EventSeverity::Info:
        return "Info";
    case EventSeverity::Warning:
        return "Warning";
    case EventSeverity::Critical:
        return "Critical";
    }

    return "Unknown";
}

[[nodiscard]] std::string idText(const std::int64_t value) {
    return std::to_string(value);
}

[[nodiscard]] std::string eventTypeName(const SimEventPayload& payload) {
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;

        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            return "mineral_extracted";
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            return "shipyard_order_created";
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            return "ship_completed";
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            return "fleet_order_assigned";
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            return "fleet_arrived";
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            return "command_rejected";
        }
    }, payload);
}

[[nodiscard]] std::string eventMessage(const SimEventPayload& payload) {
    return std::visit([](const auto& event) -> std::string {
        using Event = std::decay_t<decltype(event)>;

        std::ostringstream out;
        if constexpr (std::is_same_v<Event, MineralExtractedEvent>) {
            out << "Extracted " << event.amount << ' ' << toString(event.mineral)
                << " at body " << idText(event.bodyId.value);
        } else if constexpr (std::is_same_v<Event, ShipyardOrderCreatedEvent>) {
            out << "Created shipyard order " << idText(event.orderId.value)
                << " for " << event.quantity << " ship(s)";
        } else if constexpr (std::is_same_v<Event, ShipCompletedEvent>) {
            out << "Completed ship " << idText(event.shipId.value)
                << " for order " << idText(event.orderId.value);
        } else if constexpr (std::is_same_v<Event, FleetOrderAssignedEvent>) {
            out << "Assigned fleet " << idText(event.fleetId.value)
                << " to body " << idText(event.destinationBodyId.value)
                << " in " << event.daysRemaining << " day(s)";
        } else if constexpr (std::is_same_v<Event, FleetArrivedEvent>) {
            out << "Fleet " << idText(event.fleetId.value)
                << " arrived at body " << idText(event.destinationBodyId.value);
        } else if constexpr (std::is_same_v<Event, CommandRejectedEvent>) {
            out << event.reason;
        }
        return out.str();
    }, payload);
}

[[nodiscard]] EventLogEntrySummary summarizeEvent(const SimEvent& event) {
    return EventLogEntrySummary{
        .id = event.id,
        .day = event.day,
        .severity = event.severity,
        .severityName = severityName(event.severity),
        .eventType = eventTypeName(event.payload),
        .message = eventMessage(event.payload)
    };
}

} // namespace

SimulationQueries::SimulationQueries(const SimulationService& service) noexcept
    : service_{service} {}

std::vector<ColonySummary> SimulationQueries::colonies() const {
    const GameState& state = service_.state();
    std::vector<ColonySummary> summaries;
    summaries.reserve(state.colonies.size());

    for (const Colony& colony : state.colonies) {
        summaries.push_back(ColonySummary{
            .id = colony.id,
            .bodyId = colony.bodyId,
            .name = colony.name,
            .bodyName = bodyName(state, colony.bodyId),
            .mines = colony.mines,
            .shipyardCapacity = colony.shipyardCapacity
        });
    }

    return summaries;
}

std::vector<ShipyardOrderSummary> SimulationQueries::shipyardOrders() const {
    const GameState& state = service_.state();
    std::vector<ShipyardOrderSummary> summaries;
    summaries.reserve(state.shipyardOrders.size());

    for (const ShipyardOrder& order : state.shipyardOrders) {
        summaries.push_back(ShipyardOrderSummary{
            .id = order.id,
            .colonyId = order.colonyId,
            .shipClassId = order.shipClassId,
            .colonyName = colonyName(state, order.colonyId),
            .shipClassName = shipClassName(state, order.shipClassId),
            .quantityRequested = order.quantityRequested,
            .quantityCompleted = order.quantityCompleted,
            .accumulatedBuildPoints = order.accumulatedBuildPoints,
            .requiredBuildPoints = shipClassBuildPoints(state, order.shipClassId),
            .status = order.status,
            .statusName = statusName(order.status)
        });
    }

    return summaries;
}

std::vector<ShipClassSummary> SimulationQueries::shipClasses() const {
    const GameState& state = service_.state();
    std::vector<ShipClassSummary> summaries;
    summaries.reserve(state.shipClasses.size());

    for (const ShipClass& shipClass : state.shipClasses) {
        summaries.push_back(ShipClassSummary{
            .id = shipClass.id,
            .name = shipClass.name,
            .role = shipClass.role,
            .roleName = shipRoleName(shipClass.role),
            .buildPoints = shipClass.buildPoints
        });
    }

    return summaries;
}

std::vector<FleetSummary> SimulationQueries::fleets() const {
    const GameState& state = service_.state();
    std::vector<FleetSummary> summaries;
    summaries.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        summaries.push_back(FleetSummary{
            .id = fleet.id,
            .name = fleet.name,
            .currentBodyId = fleet.currentBodyId,
            .currentBodyName = bodyName(state, fleet.currentBodyId),
            .destinationBodyId = fleet.destinationBodyId,
            .destinationBodyName = fleet.destinationBodyId.has_value()
                ? bodyName(state, *fleet.destinationBodyId)
                : std::string{},
            .shipCount = fleet.shipIds.size(),
            .activeOrderType = fleet.activeOrder.type,
            .activeOrderName = fleetOrderName(fleet.activeOrder.type),
            .daysRemaining = fleet.activeOrder.daysRemaining
        });
    }

    return summaries;
}

std::vector<StrategicBodySummary> SimulationQueries::strategicBodies() const {
    const GameState& state = service_.state();
    std::vector<StrategicBodySummary> summaries;
    summaries.reserve(state.bodies.size());

    for (const Body& body : state.bodies) {
        summaries.push_back(StrategicBodySummary{
            .id = body.id,
            .name = body.name,
            .type = body.type,
            .typeName = bodyTypeName(body.type),
            .x = body.x,
            .y = body.y
        });
    }

    return summaries;
}

std::vector<StrategicFleetSummary> SimulationQueries::strategicFleets() const {
    const GameState& state = service_.state();
    std::vector<StrategicFleetSummary> summaries;
    summaries.reserve(state.fleets.size());

    for (const Fleet& fleet : state.fleets) {
        const Body* currentBody = bodyById(state, fleet.currentBodyId);
        const Body* destinationBody = fleet.destinationBodyId.has_value()
            ? bodyById(state, *fleet.destinationBodyId)
            : nullptr;

        summaries.push_back(StrategicFleetSummary{
            .id = fleet.id,
            .name = fleet.name,
            .currentBodyId = fleet.currentBodyId,
            .x = currentBody == nullptr ? 0.0 : currentBody->x,
            .y = currentBody == nullptr ? 0.0 : currentBody->y,
            .destinationBodyId = fleet.destinationBodyId,
            .destinationX = destinationBody == nullptr ? 0.0 : destinationBody->x,
            .destinationY = destinationBody == nullptr ? 0.0 : destinationBody->y,
            .moving = fleet.activeOrder.type == FleetOrderType::MoveToBody && fleet.destinationBodyId.has_value(),
            .daysRemaining = fleet.activeOrder.daysRemaining
        });
    }

    return summaries;
}

std::vector<EventLogEntrySummary> SimulationQueries::recentEvents(const std::size_t limit) const {
    const std::vector<SimEvent>& events = service_.state().eventLog;
    if (limit == 0 || events.empty()) {
        return {};
    }

    // Keep chronological order inside the visible tail so append-only UI widgets
    // can render the returned rows without reversing them first.
    const std::size_t start = events.size() > limit ? events.size() - limit : 0;

    std::vector<EventLogEntrySummary> summaries;
    summaries.reserve(events.size() - start);
    for (std::size_t index = start; index < events.size(); ++index) {
        summaries.push_back(summarizeEvent(events[index]));
    }

    return summaries;
}

} // namespace deep

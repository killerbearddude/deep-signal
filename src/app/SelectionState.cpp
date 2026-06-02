#include "app/SelectionState.h"

// Implements shared selection state for UI panels.
// The class is intentionally tiny and value-owned so selection can move through
// the app layer without introducing QObject-style lifetime or raw pointer risks.

namespace deep {

void SelectionState::clear() noexcept {
    type_ = SelectedObjectType::None;
    selectedId_ = 0;
}

void SelectionState::selectBody(const BodyId id) noexcept {
    type_ = SelectedObjectType::Body;
    selectedId_ = id.value;
}

void SelectionState::selectColony(const ColonyId id) noexcept {
    type_ = SelectedObjectType::Colony;
    selectedId_ = id.value;
}

void SelectionState::selectFleet(const FleetId id) noexcept {
    type_ = SelectedObjectType::Fleet;
    selectedId_ = id.value;
}

SelectedObjectType SelectionState::type() const noexcept {
    return type_;
}

std::int64_t SelectionState::selectedId() const noexcept {
    return selectedId_;
}

BodyId SelectionState::bodyId() const noexcept {
    return type_ == SelectedObjectType::Body ? BodyId{selectedId_} : BodyId{};
}

ColonyId SelectionState::colonyId() const noexcept {
    return type_ == SelectedObjectType::Colony ? ColonyId{selectedId_} : ColonyId{};
}

FleetId SelectionState::fleetId() const noexcept {
    return type_ == SelectedObjectType::Fleet ? FleetId{selectedId_} : FleetId{};
}

bool SelectionState::isBodySelected(const BodyId id) const noexcept {
    return type_ == SelectedObjectType::Body && selectedId_ == id.value;
}

bool SelectionState::isColonySelected(const ColonyId id) const noexcept {
    return type_ == SelectedObjectType::Colony && selectedId_ == id.value;
}

bool SelectionState::isFleetSelected(const FleetId id) const noexcept {
    return type_ == SelectedObjectType::Fleet && selectedId_ == id.value;
}

} // namespace deep

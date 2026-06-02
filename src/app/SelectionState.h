#pragma once

// Declares shared application selection state for read-only UI inspection.
// UI panels write only typed IDs through this small app-layer object instead of
// retaining pointers or references into GameState-owned vectors.

#include <compare>
#include <cstdint>

#include "sim/IdTypes.h"

namespace deep {

// Describes the kind of simulation object currently selected by the UI.
// The enum intentionally stays app/UI-neutral so selection can be shared by map,
// table, inspector, and future command panels without depending on ImGui.
enum class SelectedObjectType {
    None,
    Body,
    Colony,
    Fleet
};

// Stores one selected object ID plus its type. IDs are stored as raw persisted
// values internally because the active type determines which strong ID wrapper
// should be reconstructed by readers.
class SelectionState {
public:
    // Clears the current selection. Used when clicks miss selectable objects or
    // when future workflows delete a selected entity.
    void clear() noexcept;

    // Selects a body by stable simulation ID.
    void selectBody(BodyId id) noexcept;

    // Selects a colony by stable simulation ID.
    void selectColony(ColonyId id) noexcept;

    // Selects a fleet by stable simulation ID.
    void selectFleet(FleetId id) noexcept;

    // Returns the selected object kind. None means selectedId() is invalid.
    [[nodiscard]] SelectedObjectType type() const noexcept;

    // Returns the selected raw ID value for display and same-type matching.
    [[nodiscard]] std::int64_t selectedId() const noexcept;

    // Typed accessors return invalid ID wrappers unless the stored type matches.
    [[nodiscard]] BodyId bodyId() const noexcept;
    [[nodiscard]] ColonyId colonyId() const noexcept;
    [[nodiscard]] FleetId fleetId() const noexcept;

    // Convenience helpers keep UI row-selection code readable.
    [[nodiscard]] bool isBodySelected(BodyId id) const noexcept;
    [[nodiscard]] bool isColonySelected(ColonyId id) const noexcept;
    [[nodiscard]] bool isFleetSelected(FleetId id) const noexcept;

private:
    SelectedObjectType type_ = SelectedObjectType::None;
    std::int64_t selectedId_ = 0;
};

} // namespace deep

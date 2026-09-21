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

// Stores one app-local selected ID plus its type; owns no simulation entities and
// does not persist itself. The type selects the strong ID wrapper reconstructed
// by readers. Setters do not validate positivity or existence in the active game.
// The UI owner must clear/reconcile selection after new/load/delete: stable IDs
// identify entities within one world, and a replacement world may reuse them.
class SelectionState {
public:
    // Clears UI selection only; never changes or deletes a simulation entity.
    void clear() noexcept;

    // Selects a body by stable simulation ID.
    void selectBody(BodyId id) noexcept;

    // Selects a colony by stable simulation ID.
    void selectColony(ColonyId id) noexcept;

    // Selects a fleet by stable simulation ID.
    void selectFleet(FleetId id) noexcept;

    // Returns the selected object kind. None means selectedId() is zero; a
    // non-None kind alone does not establish that its ID still exists.
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

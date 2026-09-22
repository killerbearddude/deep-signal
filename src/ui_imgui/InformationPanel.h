#pragma once

// Responsibility: render the shell's persistent, read-only selection overview.
// The application owns selection and layout; queries own the projection boundary.
// This component retains no selected ID, DTO, entity, command, or preview state.

struct ImVec2;

namespace deep {
class SelectionState;
class SimulationQueries;
}

namespace deep::ui_imgui {

class InformationPanel {
public:
    // Resolves fresh owned DTOs for this render. Empty or unresolved selection
    // never triggers fallback selection or a gameplay operation.
    void render(const SimulationQueries& queries,
                const SelectionState& selection,
                const ImVec2& position,
                const ImVec2& size) const;
};

} // namespace deep::ui_imgui

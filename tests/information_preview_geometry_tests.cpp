#include "ui_imgui/InformationPreviewGeometry.h"

// Placement and confinement are pure shell geometry. These checks need no
// renderer, simulation service, SDL window, or ImGui context.

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using deep::PreviewId;
using deep::WorldGeneration;
using deep::ui_imgui::ShellRegion;
using deep::ui_imgui::confineFloatingWindow;
using deep::ui_imgui::initialPreviewGeometry;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(const float a, const float b) {
    return std::abs(a - b) < 0.001F;
}

void inside(const ShellRegion window, const ShellRegion work) {
    require(window.width >= 0.0F && window.height >= 0.0F, "nonnegative preview size");
    require(window.x >= work.x && window.y >= work.y, "preview starts inside left work area");
    require(window.x + window.width <= work.x + work.width &&
            window.y + window.height <= work.y + work.height,
            "preview ends inside left work area");
}

void normalPlacementAndOffsets() {
    const ShellRegion left{0.0F, 20.0F, 922.0F, 700.0F};
    const ShellRegion information{922.0F, 20.0F, 358.0F, 700.0F};
    const auto first = initialPreviewGeometry(PreviewId{WorldGeneration{1}, 1}, left);
    require(near(first.x, 596.0F) && near(first.y, 36.0F) &&
            near(first.width, 310.0F) && near(first.height, 340.0F),
            "first preview starts near right side at provisional size");
    for (const auto number : std::array<std::uint64_t, 4>{2, 3, 4, 5}) {
        const auto next = initialPreviewGeometry(PreviewId{WorldGeneration{1}, number}, left);
        inside(next, left);
        require(next.x != first.x && next.y != first.y,
                "adjacent preview IDs do not exactly stack on first window");
        require(next.x + next.width <= information.x,
                "preview placement does not cover the information region");
    }
}

void narrowAndTranslatedWorkAreas() {
    const ShellRegion narrow{10.0F, 30.0F, 200.0F, 180.0F};
    for (const auto number : std::array<std::uint64_t, 4>{1, 2, 3, 8}) {
        const auto preview = initialPreviewGeometry(PreviewId{WorldGeneration{1}, number}, narrow);
        inside(preview, narrow);
        require(near(preview.width, narrow.width) && near(preview.height, narrow.height),
                "oversized initial preview shrinks to narrow work region");
    }

    const ShellRegion origin{0.0F, 20.0F, 922.0F, 700.0F};
    const ShellRegion translated{-320.0F, 145.0F, 922.0F, 700.0F};
    const auto a = initialPreviewGeometry(PreviewId{WorldGeneration{5}, 2}, origin);
    const auto b = initialPreviewGeometry(PreviewId{WorldGeneration{5}, 2}, translated);
    inside(b, translated);
    require(near(b.x, a.x - 320.0F) && near(b.y, a.y + 125.0F) &&
            near(b.width, a.width) && near(b.height, a.height),
            "placement follows a translated shell without a second coordinate system");
}

void floatingRecoveryAtEdges() {
    const ShellRegion left{50.0F, 25.0F, 500.0F, 400.0F};
    const auto right = confineFloatingWindow({500.0F, 80.0F, 280.0F, 240.0F}, left);
    const auto bottom = confineFloatingWindow({130.0F, 350.0F, 280.0F, 240.0F}, left);
    const auto oversized = confineFloatingWindow({900.0F, 900.0F, 800.0F, 700.0F}, left);
    inside(right, left);
    inside(bottom, left);
    inside(oversized, left);
    require(near(right.x, 270.0F) && near(right.y, 80.0F), "right edge recovery moves only x");
    require(near(bottom.x, 130.0F) && near(bottom.y, 185.0F), "bottom edge recovery moves only y");
    require(near(oversized.x, left.x) && near(oversized.y, left.y) &&
            near(oversized.width, left.width) && near(oversized.height, left.height),
            "oversized preview fits the whole left region");
}

} // namespace

int main() {
    try {
        normalPlacementAndOffsets();
        narrowAndTranslatedWorkAreas();
        floatingRecoveryAtEdges();
        std::cout << "Information preview geometry checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

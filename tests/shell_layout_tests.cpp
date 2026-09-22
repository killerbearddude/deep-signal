#include "ui_imgui/ShellLayout.h"

// Pure shell geometry checks: no renderer, windowing, simulation, or save layer.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using deep::ui_imgui::ShellLayout;
using deep::ui_imgui::ShellRegion;
using deep::ui_imgui::calculateShellLayout;
using deep::ui_imgui::confineFloatingWindow;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.001F;
}

void requirePartition(const ShellLayout& layout, ShellRegion work) {
    const auto& left = layout.dock;
    const auto& right = layout.information;
    for (const auto& region : {left, right}) {
        require(std::isfinite(region.x) && std::isfinite(region.y) &&
                    std::isfinite(region.width) && std::isfinite(region.height),
                "shell geometry is finite");
        require(region.width >= 0.0F && region.height >= 0.0F, "shell dimensions are nonnegative");
        require(region.y >= work.y && region.y + region.height <= work.y + std::max(0.0F, work.height),
                "shell stays within the available vertical region");
    }
    require(near(left.x, work.x), "dock starts at work origin");
    require(near(left.x + left.width, right.x), "regions meet without overlap or a gap");
    require(near(right.x + right.width, work.x + std::max(0.0F, work.width)),
            "information panel ends at the work-area edge");
    require(near(left.y, right.y) && near(left.height, right.height), "regions share vertical bounds");
}

void normalWidthsAndTransitions() {
    for (const auto [width, expected] : std::array<std::array<float, 2>, 9>{{
             {599.0F, 299.0F}, {600.0F, 300.0F}, {1000.0F, 300.0F},
             {1071.0F, 300.0F}, {1072.0F, 300.0F}, {1280.0F, 358.0F},
             {1499.0F, 419.0F}, {1500.0F, 420.0F}, {1600.0F, 420.0F}}}) {
        const ShellRegion work{0.0F, 20.0F, width, 700.0F};
        const auto layout = calculateShellLayout(work, 20.0F);
        requirePartition(layout, work);
        require(near(layout.information.width, expected), "provisional panel width follows its boundaries");
        require(layout.drawable(), "ordinary shell dimensions are drawable");
    }
}

void smallAndUnavailableWorkAreas() {
    for (const float width : {-10.0F, 0.0F, 0.5F, 1.0F, 2.0F, 7.0F, 8.0F, 10.0F, 299.0F}) {
        for (const float height : {-10.0F, 0.0F, 0.5F, 1.0F, 3.0F, 4.0F, 200.0F}) {
            const ShellRegion work{75.0F, 30.0F, width, height};
            const auto layout = calculateShellLayout(work, 30.0F);
            requirePartition(layout, work);
            require(layout.drawable() == (width >= 8.0F && height >= 4.0F),
                    "regions below the dockspace minimum use only keep-alive mode");
        }
    }
    const ShellRegion work{75.0F, 30.0F, 500.0F, 10.0F};
    const auto menuConsumesHeight = calculateShellLayout(work, 100.0F);
    requirePartition(menuConsumesHeight, work);
    require(!menuConsumesHeight.drawable() && near(menuConsumesHeight.dock.height, 0.0F),
            "a menu beyond the work bottom leaves zero height without overflow");
}

void translatedViewport() {
    const auto origin = calculateShellLayout({0.0F, 20.0F, 1280.0F, 700.0F}, 20.0F);
    const ShellRegion work{-320.0F, 145.0F, 1280.0F, 700.0F};
    const auto translated = calculateShellLayout(work, 145.0F);
    requirePartition(translated, work);
    require(near(translated.dock.x, origin.dock.x - 320.0F) &&
                near(translated.information.x, origin.information.x - 320.0F),
            "both regions follow viewport horizontal translation");
    require(near(translated.dock.y, origin.dock.y + 125.0F) &&
                near(translated.dock.width, origin.dock.width) &&
                near(translated.dock.height, origin.dock.height),
            "translation preserves available size");
}

void firstFrameMenuAndFollowingWorkInset() {
    const auto first = calculateShellLayout({50.0F, 100.0F, 1280.0F, 720.0F}, 123.0F);
    const auto next = calculateShellLayout({50.0F, 123.0F, 1280.0F, 697.0F}, 123.0F);
    require(near(first.dock.y, 123.0F) && near(first.dock.height, 697.0F),
            "actual first-frame menu bottom reserves shell space");
    require(near(next.dock.y, first.dock.y) && near(next.dock.height, first.dock.height),
            "viewport work inset does not reserve the menu twice");
    const auto existingInset = calculateShellLayout({50.0F, 130.0F, 1280.0F, 690.0F}, 123.0F);
    require(near(existingInset.dock.y, 130.0F) && near(existingInset.dock.height, 690.0F),
            "an existing larger work inset remains authoritative");
}

void resizingDoesNotRetainGeometry() {
    for (const auto [width, expected] : std::array<std::array<float, 2>, 5>{{
             {1600.0F, 420.0F}, {400.0F, 200.0F}, {0.0F, 0.0F},
             {1280.0F, 358.0F}, {1600.0F, 420.0F}}}) {
        const ShellRegion work{15.0F, 40.0F, width, 500.0F};
        const auto layout = calculateShellLayout(work, 40.0F);
        requirePartition(layout, work);
        require(near(layout.information.width, expected) && near(layout.dock.height, 500.0F),
                "resize uses the current work rectangle after shrink, minimize, and restore");
    }
}

void floatingWindowRecovery() {
    struct Case {
        ShellRegion work;
        ShellRegion window;
        ShellRegion expected;
        const char* description;
    };
    const ShellRegion work{0.0F, 20.0F, 922.0F, 700.0F};
    const std::array<Case, 6> cases{{
        {work, {100.0F, 100.0F, 300.0F, 200.0F}, {100.0F, 100.0F, 300.0F, 200.0F},
            "an already-contained floating window retains position and size"},
        {work, {800.0F, 200.0F, 300.0F, 200.0F}, {622.0F, 200.0F, 300.0F, 200.0F},
            "crossing the information reservation moves only the overflowing axis"},
        {work, {1600.0F, 1500.0F, 300.0F, 200.0F}, {622.0F, 520.0F, 300.0F, 200.0F},
            "a completely offscreen window recovers at the nearest available edge"},
        {work, {-100.0F, -200.0F, 300.0F, 200.0F}, {0.0F, 20.0F, 300.0F, 200.0F},
            "negative offscreen coordinates recover below the menu"},
        {work, {200.0F, 100.0F, 1400.0F, 1000.0F}, {0.0F, 20.0F, 922.0F, 700.0F},
            "an oversized floating window shrinks deterministically to the work region"},
        {{-320.0F, 145.0F, 922.0F, 700.0F}, {400.0F, 700.0F, 300.0F, 200.0F},
            {302.0F, 645.0F, 300.0F, 200.0F}, "recovery follows a translated work rectangle"}
    }};
    for (const auto& example : cases) {
        const auto result = confineFloatingWindow(example.window, example.work);
        require(near(result.x, example.expected.x) && near(result.y, example.expected.y) &&
                    near(result.width, example.expected.width) && near(result.height, example.expected.height),
                example.description);
        const auto again = confineFloatingWindow(result, example.work);
        require(near(again.x, result.x) && near(again.y, result.y) &&
                    near(again.width, result.width) && near(again.height, result.height),
                "repeated confinement is idempotent and does not drift the window");
    }
}

void floatingWindowsInUnavailableOrNarrowRegions() {
    for (const float width : {-10.0F, 0.0F, 0.5F, 1.0F, 30.0F}) {
        for (const float height : {-10.0F, 0.0F, 0.5F, 1.0F, 30.0F}) {
            const ShellRegion work{75.0F, 30.0F, width, height};
            const auto result = confineFloatingWindow({100.0F, 100.0F, 300.0F, 200.0F}, work);
            require(near(result.x, work.x) && near(result.y, work.y) &&
                        near(result.width, std::max(0.0F, width)) &&
                        near(result.height, std::max(0.0F, height)),
                    "unavailable or narrow regions produce nonnegative contained geometry without overflow");
            const auto again = confineFloatingWindow(result, work);
            require(near(again.x, result.x) && near(again.y, result.y) &&
                        near(again.width, result.width) && near(again.height, result.height),
                    "degenerate confinement is stable across repeated frames");
        }
    }
}

} // namespace

int main() {
    try {
        normalWidthsAndTransitions();
        smallAndUnavailableWorkAreas();
        translatedViewport();
        firstFrameMenuAndFollowingWorkInset();
        resizingDoesNotRetainGeometry();
        floatingWindowRecovery();
        floatingWindowsInUnavailableOrNarrowRegions();
        std::cout << "Shell layout: 7 scenarios passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Shell layout failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

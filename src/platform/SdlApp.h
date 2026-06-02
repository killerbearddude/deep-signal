#pragma once

// Declares a small RAII wrapper around SDL3 window and renderer state.
// The platform layer owns native SDL resources only; simulation and app-layer
// state remain outside this wrapper so the UI shell cannot mutate game rules.

#include <SDL3/SDL.h>

#include <string>

namespace deep::platform {

// Owns the SDL video subsystem, one window, and one accelerated renderer.
// The object is intentionally move/copy disabled because SDL resources have
// pointer identity and must be destroyed exactly once.
class SdlApp {
public:
    // Initializes SDL video, creates the main window, and creates a renderer.
    // Throws std::runtime_error when SDL cannot create a required resource.
    SdlApp(std::string title, int width, int height);

    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;
    SdlApp(SdlApp&&) = delete;
    SdlApp& operator=(SdlApp&&) = delete;

    // Destroys renderer/window resources and shuts down the SDL video subsystem.
    ~SdlApp();

    // Returns the owned SDL window for ImGui backend initialization.
    [[nodiscard]] SDL_Window* window() const noexcept;

    // Returns the owned SDL renderer for ImGui backend rendering.
    [[nodiscard]] SDL_Renderer* renderer() const noexcept;

    // Converts SDL quit/window-close events into the shell's run-loop exit signal.
    [[nodiscard]] bool isQuitEvent(const SDL_Event& event) const noexcept;

    // Clears the render target before ImGui draws a new frame.
    void beginFrame() const;

    // Presents the completed ImGui frame to the window.
    void endFrame() const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
};

} // namespace deep::platform

#pragma once

// Declares a small RAII wrapper around SDL3 window and renderer state.
// The platform layer owns native SDL resources only; simulation and app-layer
// state remain outside this wrapper so the UI shell cannot mutate game rules.

#include <SDL3/SDL.h>

#include <string>

namespace deep::platform {

// Owns SDL initialization, one window, and the renderer chosen by SDL.
// The object is intentionally move/copy disabled because SDL resources have
// pointer identity and must be destroyed exactly once.
// Assumption: one application-level SDL owner on the UI thread. Destruction
// calls SDL_Quit(), so independent SDL users would require shared lifetime control.
class SdlApp {
public:
    // Initializes SDL video, creates the main window, and creates a renderer.
    // Throws std::runtime_error when SDL cannot create a required resource.
    // Releases earlier resources if a later creation step fails. Width/height
    // are SDL window coordinate units; they are not simulation map units.
    SdlApp(std::string title, int width, int height);

    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;
    SdlApp(SdlApp&&) = delete;
    SdlApp& operator=(SdlApp&&) = delete;

    // Destroys renderer/window resources and shuts down SDL. ImGui backends must
    // release their use of these resources before this destructor runs.
    ~SdlApp();

    // Borrows the owned window until this SdlApp is destroyed; do not destroy it.
    [[nodiscard]] SDL_Window* window() const noexcept;

    // Borrows the owned renderer until this SdlApp is destroyed; do not destroy it.
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

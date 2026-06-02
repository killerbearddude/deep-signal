#include "platform/SdlApp.h"

// Implements the SDL3 platform shell for the Dear ImGui prototype.
// This file deliberately contains no simulation logic; it only creates and
// cleans up native windowing/rendering resources.

#include <stdexcept>
#include <string>
#include <utility>

namespace deep::platform {
namespace {

[[nodiscard]] std::runtime_error sdlError(const std::string& operation) {
    return std::runtime_error{operation + ": " + SDL_GetError()};
}

} // namespace

SdlApp::SdlApp(std::string title, const int width, const int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw sdlError("SDL_Init(SDL_INIT_VIDEO) failed");
    }

    window_ = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        const std::runtime_error error = sdlError("SDL_CreateWindow failed");
        SDL_Quit();
        throw error;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (renderer_ == nullptr) {
        const std::runtime_error error = sdlError("SDL_CreateRenderer failed");
        SDL_DestroyWindow(std::exchange(window_, nullptr));
        SDL_Quit();
        throw error;
    }

    SDL_SetRenderVSync(renderer_, 1);
}

SdlApp::~SdlApp() {
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}

SDL_Window* SdlApp::window() const noexcept {
    return window_;
}

SDL_Renderer* SdlApp::renderer() const noexcept {
    return renderer_;
}

bool SdlApp::isQuitEvent(const SDL_Event& event) const noexcept {
    if (event.type == SDL_EVENT_QUIT) {
        return true;
    }

    // Limit window close handling to the window owned by this shell so future
    // auxiliary tool windows cannot accidentally terminate the application.
    return event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
        && event.window.windowID == SDL_GetWindowID(window_);
}

void SdlApp::beginFrame() const {
    SDL_SetRenderDrawColor(renderer_, 18, 18, 24, 255);
    SDL_RenderClear(renderer_);
}

void SdlApp::endFrame() const {
    SDL_RenderPresent(renderer_);
}

} // namespace deep::platform

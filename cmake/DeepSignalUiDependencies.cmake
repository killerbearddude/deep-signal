# Configures the optional SDL3/Dear ImGui dependency bundle for the desktop UI.
# Headless simulation, save, app, and test targets must not include this file's
# third-party source target unless DEEP_SIGNAL_BUILD_UI is explicitly enabled.

function(deep_signal_configure_ui_dependencies out_target)
    find_package(SDL3 CONFIG REQUIRED)

    if(NOT TARGET SDL3::SDL3)
        message(FATAL_ERROR "SDL3 package was found, but target SDL3::SDL3 is unavailable")
    endif()

    set(DEEP_SIGNAL_IMGUI_ROOT "${PROJECT_SOURCE_DIR}/third_party/imgui" CACHE PATH
        "Path to the Dear ImGui source checkout used by the optional UI target")
    set(DEEP_SIGNAL_IMPLOT_ROOT "${PROJECT_SOURCE_DIR}/third_party/implot" CACHE PATH
        "Path to the ImPlot source checkout used by the optional UI target")

    set(_required_imgui_files
        imgui.h
        imgui.cpp
        imgui_draw.cpp
        imgui_tables.cpp
        imgui_widgets.cpp
        backends/imgui_impl_sdl3.h
        backends/imgui_impl_sdl3.cpp
        backends/imgui_impl_sdlrenderer3.h
        backends/imgui_impl_sdlrenderer3.cpp
    )

    foreach(_relative_path IN LISTS _required_imgui_files)
        if(NOT EXISTS "${DEEP_SIGNAL_IMGUI_ROOT}/${_relative_path}")
            message(FATAL_ERROR
                "Dear ImGui source file is missing: ${DEEP_SIGNAL_IMGUI_ROOT}/${_relative_path}\n"
                "Initialize the expected source checkout before building with DEEP_SIGNAL_BUILD_UI=ON.\n"
                "See README.md and third_party/imgui/README.md for the supported setup.")
        endif()
    endforeach()

    set(_required_implot_files
        implot.h
        implot_internal.h
        implot.cpp
        implot_items.cpp
    )

    foreach(_relative_path IN LISTS _required_implot_files)
        if(NOT EXISTS "${DEEP_SIGNAL_IMPLOT_ROOT}/${_relative_path}")
            message(FATAL_ERROR
                "ImPlot source file is missing: ${DEEP_SIGNAL_IMPLOT_ROOT}/${_relative_path}\n"
                "Initialize the expected source checkout before building with DEEP_SIGNAL_BUILD_UI=ON.\n"
                "See README.md and third_party/implot/README.md for the supported setup.")
        endif()
    endforeach()

    add_library(deep_signal_imgui_vendor STATIC
        "${DEEP_SIGNAL_IMGUI_ROOT}/imgui.cpp"
        "${DEEP_SIGNAL_IMGUI_ROOT}/imgui_draw.cpp"
        "${DEEP_SIGNAL_IMGUI_ROOT}/imgui_tables.cpp"
        "${DEEP_SIGNAL_IMGUI_ROOT}/imgui_widgets.cpp"
        "${DEEP_SIGNAL_IMGUI_ROOT}/backends/imgui_impl_sdl3.cpp"
        "${DEEP_SIGNAL_IMGUI_ROOT}/backends/imgui_impl_sdlrenderer3.cpp"
        "${DEEP_SIGNAL_IMPLOT_ROOT}/implot.cpp"
        "${DEEP_SIGNAL_IMPLOT_ROOT}/implot_items.cpp"
    )

    target_include_directories(deep_signal_imgui_vendor
        PUBLIC
            "${DEEP_SIGNAL_IMGUI_ROOT}"
            "${DEEP_SIGNAL_IMGUI_ROOT}/backends"
            "${DEEP_SIGNAL_IMPLOT_ROOT}"
    )

    # SDL include and link requirements must propagate because the project UI
    # sources include SDL3 headers directly and the ImGui backends compile
    # against SDL3's renderer API.
    target_link_libraries(deep_signal_imgui_vendor PUBLIC SDL3::SDL3)
    target_compile_features(deep_signal_imgui_vendor PUBLIC cxx_std_20)

    set(${out_target} deep_signal_imgui_vendor PARENT_SCOPE)
endfunction()

# Builds the headless Deep Signal prototype.
# The simulation target must remain independent from UI, rendering, and database
# libraries so it can be tested and reused by future CLI, SQLite, and ImGui layers.
cmake_minimum_required(VERSION 3.22)

project(DeepSignal
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DEEP_SIGNAL_BUILD_TESTS "Build headless simulation tests" ON)
option(DEEP_SIGNAL_BUILD_SAVE "Build SQLite save/load layer" ON)
option(DEEP_SIGNAL_BUILD_APP "Build application service and CLI" ON)
option(DEEP_SIGNAL_BUILD_UI "Build SDL3/Dear ImGui desktop shell" OFF)


# Shared warning set for project-owned C++ targets. Warnings are intentionally
# strict because this prototype is small enough to keep warning-clean.
set(DEEP_SIGNAL_WARNINGS
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion>
)

add_library(deep_signal_sim
    src/sim/GameStateValidation.cpp
    src/sim/Simulation.cpp
    src/sim/ScenarioFactory.cpp
)

target_include_directories(deep_signal_sim
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(deep_signal_sim PUBLIC cxx_std_20)
target_compile_options(deep_signal_sim PRIVATE ${DEEP_SIGNAL_WARNINGS})

if(DEEP_SIGNAL_BUILD_SAVE)
    find_package(SQLite3 REQUIRED)

    include(CheckIncludeFileCXX)
    check_include_file_cxx("nlohmann/json.hpp" DEEP_SIGNAL_HAS_SYSTEM_NLOHMANN_JSON)
    if(DEEP_SIGNAL_HAS_SYSTEM_NLOHMANN_JSON)
        message(STATUS "Found nlohmann/json.hpp; EventJson will use the system header")
    else()
        message(STATUS "nlohmann/json.hpp not found; EventJson will use the schema-v1 fallback parser")
    endif()

    add_library(deep_signal_save
        src/save/Database.cpp
        src/save/EventJson.cpp
        src/save/Schema.cpp
        src/save/SaveGameRepository.cpp
    )

    target_link_libraries(deep_signal_save
        PUBLIC deep_signal_sim SQLite::SQLite3
    )

    target_compile_options(deep_signal_save PRIVATE ${DEEP_SIGNAL_WARNINGS})
endif()

if(DEEP_SIGNAL_BUILD_APP)
    if(NOT DEEP_SIGNAL_BUILD_SAVE)
        message(FATAL_ERROR "DEEP_SIGNAL_BUILD_APP requires DEEP_SIGNAL_BUILD_SAVE for save/load workflows")
    endif()

    add_library(deep_signal_app
        src/app/ForecastService.cpp
        src/app/SimulationQueries.cpp
        src/app/SimulationService.cpp
    )

    target_link_libraries(deep_signal_app PUBLIC deep_signal_sim deep_signal_save)
    target_compile_options(deep_signal_app PRIVATE ${DEEP_SIGNAL_WARNINGS})

    add_executable(deep_signal_cli
        src/cli/main.cpp
    )

    target_link_libraries(deep_signal_cli PRIVATE deep_signal_app)
    target_compile_options(deep_signal_cli PRIVATE ${DEEP_SIGNAL_WARNINGS})
endif()


if(DEEP_SIGNAL_BUILD_UI)
    if(NOT DEEP_SIGNAL_BUILD_APP)
        message(FATAL_ERROR "DEEP_SIGNAL_BUILD_UI requires DEEP_SIGNAL_BUILD_APP")
    endif()

    find_package(SDL3 CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)

    if(NOT TARGET SDL3::SDL3)
        message(FATAL_ERROR "SDL3 package was found, but target SDL3::SDL3 is unavailable")
    endif()

    if(NOT TARGET imgui::imgui)
        message(FATAL_ERROR "imgui package was found, but target imgui::imgui is unavailable")
    endif()

    add_executable(deep_signal_imgui
        src/platform/SdlApp.cpp
        src/ui_imgui/ColonyPanel.cpp
        src/ui_imgui/EventLogPanel.cpp
        src/ui_imgui/FleetPanel.cpp
        src/ui_imgui/ImGuiApp.cpp
        src/ui_imgui/TimeControlPanel.cpp
    )

    target_link_libraries(deep_signal_imgui
        PRIVATE deep_signal_app SDL3::SDL3 imgui::imgui
    )

    target_compile_options(deep_signal_imgui PRIVATE ${DEEP_SIGNAL_WARNINGS})
endif()

if(DEEP_SIGNAL_BUILD_TESTS)
    enable_testing()

    add_executable(deep_signal_sim_tests
        tests/sim_tests.cpp
    )

    target_link_libraries(deep_signal_sim_tests PRIVATE deep_signal_sim)
    target_compile_options(deep_signal_sim_tests PRIVATE ${DEEP_SIGNAL_WARNINGS})

    add_test(NAME deep_signal_sim_tests COMMAND deep_signal_sim_tests)

    add_executable(deep_signal_validation_tests
        tests/validation_tests.cpp
    )

    target_link_libraries(deep_signal_validation_tests PRIVATE deep_signal_sim)
    target_compile_options(deep_signal_validation_tests PRIVATE ${DEEP_SIGNAL_WARNINGS})

    add_test(NAME deep_signal_validation_tests COMMAND deep_signal_validation_tests)

    if(DEEP_SIGNAL_BUILD_SAVE)
        add_executable(deep_signal_event_json_tests
            tests/event_json_tests.cpp
        )

        target_link_libraries(deep_signal_event_json_tests PRIVATE deep_signal_save)
        target_compile_options(deep_signal_event_json_tests PRIVATE ${DEEP_SIGNAL_WARNINGS})

        add_test(NAME deep_signal_event_json_tests COMMAND deep_signal_event_json_tests)
    endif()

    if(DEEP_SIGNAL_BUILD_SAVE AND DEEP_SIGNAL_BUILD_APP)
        add_executable(deep_signal_app_query_tests
            tests/app_query_tests.cpp
        )

        target_link_libraries(deep_signal_app_query_tests PRIVATE deep_signal_app)
        target_compile_options(deep_signal_app_query_tests PRIVATE ${DEEP_SIGNAL_WARNINGS})

        add_test(NAME deep_signal_app_query_tests COMMAND deep_signal_app_query_tests)

        add_executable(deep_signal_forecast_tests
            tests/forecast_tests.cpp
        )

        target_link_libraries(deep_signal_forecast_tests PRIVATE deep_signal_app)
        target_compile_options(deep_signal_forecast_tests PRIVATE ${DEEP_SIGNAL_WARNINGS})

        add_test(NAME deep_signal_forecast_tests COMMAND deep_signal_forecast_tests)

        add_executable(deep_signal_save_load_tests
            tests/save_load_tests.cpp
        )

        target_link_libraries(deep_signal_save_load_tests PRIVATE deep_signal_app deep_signal_save)
        target_compile_options(deep_signal_save_load_tests PRIVATE ${DEEP_SIGNAL_WARNINGS})

        add_test(NAME deep_signal_save_load_tests COMMAND deep_signal_save_load_tests)
    endif()
endif()

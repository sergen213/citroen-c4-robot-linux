cmake_minimum_required(VERSION 3.24)
project(CitroenC4RobotNative VERSION 0.1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(C4_ENABLE_VULKAN "Build the independent Vulkan renderer" ON)
set(C4_VERSION "${PROJECT_VERSION}" CACHE STRING "Native game/runtime package version")
if(NOT C4_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[A-Za-z0-9.-]+)?$")
    message(FATAL_ERROR "C4_VERSION must be a semantic version without a leading v")
endif()

include(GNUInstallDirs)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
include(CTest)
find_package(PkgConfig REQUIRED)
find_package(SDL3 3.2.0 CONFIG QUIET)
if(TARGET SDL3::SDL3-static)
    set(C4_SDL_TARGET SDL3::SDL3-static)
else()
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3>=3.2.0)
    set(C4_SDL_TARGET PkgConfig::SDL3)
endif()
find_package(OpenGL REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_library(c4_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(c4_warnings INTERFACE -Wall -Wextra -Wpedantic)
    # Recovered source CPU math has explicit FLOAT rounding; never fuse its operations.
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>")
endif()

add_library(c4_settings STATIC src/core/settings.cpp)
target_include_directories(c4_settings PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(c4_settings PRIVATE c4_warnings)

add_executable(citroen-c4-robot
    src/main.cpp src/platform/runtime.cpp src/core/render_scene.cpp
    src/core/game_session.cpp src/core/game_render_bridge.cpp)
target_link_libraries(citroen-c4-robot PRIVATE c4_settings ${C4_SDL_TARGET} OpenGL::GL c4_warnings)
target_compile_definitions(citroen-c4-robot PRIVATE C4_VERSION="${C4_VERSION}")
install(TARGETS citroen-c4-robot RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS tools/import_assets.py DESTINATION "${CMAKE_INSTALL_DATADIR}/citroen-c4-robot/tools")
install(FILES tools/original-assets.json DESTINATION "${CMAKE_INSTALL_DATADIR}/citroen-c4-robot/tools")
install(FILES README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/citroen-c4-robot")
install(FILES
    docs/PROGRESS.md docs/REVERSE_ENGINEERING.md docs/ENGINE_ARCHITECTURE.md
    docs/GRAPHICS_ROADMAP.md docs/KNOWN_ISSUES.md docs/FUNCTION_MAP.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/citroen-c4-robot/docs")

# These directories supply independently owned native targets and test hooks.
add_subdirectory(src/assets)
add_subdirectory(src/audio)
add_subdirectory(src/physics)
add_subdirectory(src/rendering)
add_subdirectory(src/game)
if(C4_ENABLE_VULKAN)
    add_subdirectory(src/rendering/vulkan)
    target_link_libraries(citroen-c4-robot PRIVATE citroen_vulkan)
    target_compile_definitions(citroen-c4-robot PRIVATE C4_ENABLE_VULKAN=1)
else()
    target_compile_definitions(citroen-c4-robot PRIVATE C4_ENABLE_VULKAN=0)
endif()
add_subdirectory(launcher)
add_subdirectory(updater)
target_link_libraries(c4_settings PUBLIC native_quality)
target_link_libraries(citroen-c4-robot PRIVATE
    native_assets native_audio native_physics native_game c4_game_ui
    native_materials native_quality_gl)

if(BUILD_TESTING)
    add_executable(c4-settings-test src/core/settings_test.cpp)
    target_link_libraries(c4-settings-test PRIVATE c4_settings c4_warnings)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(c4-settings-test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME runtime-settings COMMAND c4-settings-test)
    add_test(NAME importer-safety
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s tests -p test_import_assets.py)
    set_tests_properties(importer-safety PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    find_program(C4_XVFB_EXECUTABLE NAMES Xvfb)
    find_program(C4_XVFB_RUN_EXECUTABLE NAMES xvfb-run)
    if(C4_XVFB_EXECUTABLE AND C4_XVFB_RUN_EXECUTABLE)
        add_test(NAME native-x11-window
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/test_native_window.py"
                --runtime "$<TARGET_FILE:citroen-c4-robot>"
                --xvfb-run "${C4_XVFB_RUN_EXECUTABLE}")
        set_tests_properties(native-x11-window PROPERTIES TIMEOUT 45)
    endif()
endif()

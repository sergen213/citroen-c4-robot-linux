add_library(native_quality STATIC GraphicSettings.cpp ShadowCascades.cpp QualityDiagnostics.cpp)
target_include_directories(native_quality PUBLIC
    "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(native_quality PRIVATE c4_warnings)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    quality_post.glsl quality_pbr.glsl quality_shadow.glsl)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/quality_post.glsl" C4_QUALITY_POST_SHADER)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/quality_pbr.glsl" C4_QUALITY_PBR_SHADER)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/quality_shadow.glsl" C4_QUALITY_SHADOW_SHADER)
configure_file(QualityShaders.hpp.in QualityShaders.hpp @ONLY)

add_library(native_quality_gl STATIC OpenGLQuality.cpp)
target_include_directories(native_quality_gl PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/..")
target_link_libraries(native_quality_gl PUBLIC native_quality
    PRIVATE ${C4_SDL_TARGET} OpenGL::GL c4_warnings)

if(BUILD_TESTING)
    add_executable(c4-graphic-settings-test "${PROJECT_SOURCE_DIR}/tests/test_graphic_settings.cpp")
    target_link_libraries(c4-graphic-settings-test PRIVATE native_quality c4_warnings)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(c4-graphic-settings-test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME native-graphic-settings COMMAND c4-graphic-settings-test)
endif()

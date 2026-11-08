find_package(Vulkan REQUIRED)
find_program(C4_GLSLC_EXECUTABLE NAMES glslc REQUIRED)

set(C4_VULKAN_SHADER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(C4_VULKAN_SHADER_BINARIES)
foreach(shader IN ITEMS raster.vert raster.frag compose.frag rt.comp temporal.comp fullscreen.vert)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}")
    set(binary "${C4_VULKAN_SHADER_BUILD_DIR}/${shader}.spv")
    set(shader_dependencies "${source}")
    if(shader STREQUAL "raster.vert" OR shader STREQUAL "raster.frag" OR shader STREQUAL "compose.frag")
        list(APPEND shader_dependencies
            "${PROJECT_SOURCE_DIR}/src/rendering/material_normal.glsl"
            "${PROJECT_SOURCE_DIR}/src/rendering/material_fixed.glsl"
            "${PROJECT_SOURCE_DIR}/src/rendering/material_bloom.glsl"
            "${PROJECT_SOURCE_DIR}/src/rendering/source_lighting.glsl")
    elseif(shader STREQUAL "fullscreen.vert")
        list(APPEND shader_dependencies "${PROJECT_SOURCE_DIR}/src/rendering/material_bloom.glsl")
    elseif(shader STREQUAL "rt.comp")
        list(APPEND shader_dependencies
            "${PROJECT_SOURCE_DIR}/src/rendering/material_normal.glsl"
            "${PROJECT_SOURCE_DIR}/src/rendering/material_fixed.glsl"
            "${PROJECT_SOURCE_DIR}/src/rendering/quality/quality_pbr.glsl")
    endif()
    if(shader STREQUAL "raster.frag")
        list(APPEND shader_dependencies
            "${PROJECT_SOURCE_DIR}/src/rendering/quality/quality_pbr.glsl"
            "${PROJECT_SOURCE_DIR}/src/rendering/quality/quality_post.glsl"
            "${PROJECT_SOURCE_DIR}/src/rendering/quality/quality_shadow.glsl")
    elseif(shader STREQUAL "compose.frag")
        list(APPEND shader_dependencies "${PROJECT_SOURCE_DIR}/src/rendering/quality/quality_post.glsl")
    endif()
    add_custom_command(
        OUTPUT "${binary}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${C4_VULKAN_SHADER_BUILD_DIR}"
        COMMAND "${C4_GLSLC_EXECUTABLE}" --target-env=vulkan1.3
            -I "${PROJECT_SOURCE_DIR}/src/rendering" -MD -MF "${binary}.d"
            "${source}" -o "${binary}"
        DEPENDS ${shader_dependencies}
        DEPFILE "${binary}.d"
        VERBATIM)
    list(APPEND C4_VULKAN_SHADER_BINARIES "${binary}")
endforeach()
add_custom_target(c4_vulkan_shaders DEPENDS ${C4_VULKAN_SHADER_BINARIES})

add_library(citroen_vulkan STATIC
    VulkanScene.cpp raytracing.cpp VulkanRasterTarget.cpp SourceComposite.cpp quality.cpp VulkanDispatch.cpp)
add_dependencies(citroen_vulkan c4_vulkan_shaders)
target_include_directories(citroen_vulkan PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(citroen_vulkan SYSTEM PUBLIC ${Vulkan_INCLUDE_DIRS})
target_compile_definitions(citroen_vulkan PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(citroen_vulkan PRIVATE
    ${C4_SDL_TARGET} native_assets native_materials native_quality c4_game_ui c4_warnings)
target_compile_definitions(citroen_vulkan PRIVATE
    C4_VULKAN_SHADER_BUILD_DIR="${C4_VULKAN_SHADER_BUILD_DIR}")

install(FILES ${C4_VULKAN_SHADER_BINARIES}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/citroen-c4-robot/shaders/vulkan")

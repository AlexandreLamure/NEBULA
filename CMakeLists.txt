cmake_minimum_required(VERSION 3.20)
project(OM3D)

# CPP setup
set(CMAKE_CXX_STANDARD 20)
if(MSVC)
    set(COMPILE_OPTIONS /permissive- /W3 -D_CRT_SECURE_NO_DEPRECATE /Zc:preprocessor /wd5105)
else()
    set(EXTRA_WARNINGS -Wfloat-conversion)
    set(COMPILE_OPTIONS -pedantic -Wall -Wextra ${EXTRA_WARNINGS}) # TODO: Add -Werror
endif()




# setup external libraries

# Vulkan
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found")
endif()
if(Vulkan_VERSION VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Vulkan SDK is too old. OM3D requires 1.3 or newer")
endif()

# slangc executable
if(NOT Vulkan_SLANGC_EXECUTABLE)
    set(Vulkan_SLANGC_EXECUTABLE ${Vulkan_GLSLC_EXECUTABLE})
    cmake_path(REPLACE_FILENAME Vulkan_SLANGC_EXECUTABLE "slangc")
endif()
if(NOT Vulkan_SLANGC_EXECUTABLE)
    message(FATAL_ERROR "slangc not found. Install Slang or the Vulkan SDK.")
endif()

## glfw for window handling
add_subdirectory(external/glfw)

# glm for math types
add_subdirectory(external/glm)


include_directories(external/glfw/include)
include_directories(external/glm)
include_directories(external/volk)
include_directories(external/vma)
include_directories(external)
include_directories(${Vulkan_INCLUDE_DIRS})

include_directories(${OM3D_SOURCE_DIR}/src)


file(GLOB_RECURSE SOURCE_FILES
    "src/*.h"
    "src/*.cpp"
)

file(GLOB_RECURSE EXTERNAL_FILES
    "external/volk/volk.c"
    "external/volk/volk.h"
    "external/vma/vk_mem_alloc.h"
    "external/imgui/*.cpp"
    "external/imgui/*.h"
)

# Shader files
file(GLOB_RECURSE SHADER_FILES
    "shaders/*.slang"
)
set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)



add_executable(OM3D ${SOURCE_FILES} ${EXTERNAL_FILES} ${SHADER_FILES})
target_link_libraries(OM3D glfw)
target_compile_options(OM3D PUBLIC ${COMPILE_OPTIONS})

# Bake absolute paths so they do not depend on cwd or build-dir layout.
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/data" OM3D_DATA_DIR)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}" OM3D_SHADER_DIR)
target_compile_definitions(OM3D PRIVATE
    "OM3D_DATA_PATH=\"${OM3D_DATA_DIR}/\""
    "OM3D_SHADER_PATH=\"${OM3D_SHADER_DIR}/\""
)
set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SPIRV_FILES)

macro(add_slang_shader SHADER_FILE)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(COMPILE_ARGS)
    set(NAME_SUFFIX)
    foreach(DEF ${ARGN})
        list(APPEND COMPILE_ARGS -D${DEF}=1)
        set(NAME_SUFFIX "${NAME_SUFFIX}_${DEF}")
    endforeach()

    # SPIR-V is written next to the executable
    set(SHADER_BIN ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_NAME}${NAME_SUFFIX}.spv)
    add_custom_command(
        OUTPUT ${SHADER_BIN}
        COMMAND ${Vulkan_SLANGC_EXECUTABLE}
            ${COMPILE_ARGS}
            -target spirv
            -profile spirv_1_3
            -matrix-layout-column-major
            -fvk-use-entrypoint-name
            -emit-spirv-directly
            -I ${SHADER_SOURCE_DIR}
            -o ${SHADER_BIN}
            ${SHADER_SOURCE_DIR}/${SHADER_FILE}
        DEPENDS
            ${SHADER_SOURCE_DIR}/${SHADER_FILE}
            ${SHADER_SOURCE_DIR}/structs.slang
            ${SHADER_SOURCE_DIR}/utils.slang
            ${SHADER_SOURCE_DIR}/lighting.slang
        COMMENT "slangc ${SHADER_FILE} ${ARGN}"
        VERBATIM
    )
    list(APPEND SPIRV_FILES ${SHADER_BIN})
endmacro()

add_slang_shader(basic.slang)
add_slang_shader(lit.slang)
add_slang_shader(lit.slang ALPHA_TEST)
add_slang_shader(screen.slang)
add_slang_shader(sky.slang)
add_slang_shader(tonemap.slang)
add_slang_shader(passthrough.slang)
add_slang_shader(imgui.slang)
add_slang_shader(brdf.slang)
add_slang_shader(equirec_cube.slang)

add_custom_target(shaders DEPENDS ${SPIRV_FILES})
add_dependencies(OM3D shaders)

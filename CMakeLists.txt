cmake_minimum_required(VERSION 3.20)
project(OM3D)

# CPP setup
set(CMAKE_CXX_STANDARD 20)
if(MSVC)
    set(COMPILE_OPTIONS /permissive- /W3 -D_CRT_SECURE_NO_DEPRECATE /Zc:preprocessor /wd5105)
else()
    set(EXTRA_WARNINGS -Wfloat-conversion)
    set(COMPILE_OPTIONS -pedantic -Werror -Wall -Wextra ${EXTRA_WARNINGS})
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



add_executable(OM3D ${SOURCE_FILES} ${EXTERNAL_FILES} ${SHADER_FILES})
target_link_libraries(OM3D glfw)
target_compile_options(OM3D PUBLIC ${COMPILE_OPTIONS})

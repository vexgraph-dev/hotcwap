cmake_minimum_required(VERSION 4.3)
project(hotcwap C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(APPLE)
    enable_language(OBJC)
    set(CMAKE_OBJC_STANDARD 23)
    set(CMAKE_OBJC_STANDARD_REQUIRED ON)
endif()

# The OS-stable layer: hot-reload machinery + the OS-agnostic window.
# A window never hot-updates — it is owned by the OS — so it lives here,
# next to the loader that persists across module swaps.
add_library(hotcwap STATIC
    hot/hot.c
    hot/manifest.c
    hot/vk_loader.c
    vulkan/vulkan.c
)
target_include_directories(hotcwap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(hotcwap PRIVATE -Wall -Wextra -Werror -mcpu=native)

# Vulkan headers: prefer a local SDK, fall back to Homebrew layout.
if(EXISTS "/opt/homebrew/include/vulkan/vulkan.h")
    target_include_directories(hotcwap PUBLIC /opt/homebrew/include)
endif()

# Check if vexspoke is already available as a target (parent build)
if(NOT TARGET vexspoke)
    option(HOTCWAP_USE_VEXSPOKE "Depend on vexspoke core via FetchContent" OFF)
    if(HOTCWAP_USE_VEXSPOKE)
        include(FetchContent)
        FetchContent_Declare(
            vexspoke
            GIT_REPOSITORY https://github.com/vexgraph-dev/vexspoke.git
            GIT_TAG main
        )
        FetchContent_MakeAvailable(vexspoke)
        target_include_directories(hotcwap PUBLIC ${vexspoke_SOURCE_DIR}/src)
    endif()
endif()

if(TARGET vexspoke)
    target_link_libraries(hotcwap PUBLIC vexspoke)
endif()

# OS window backend: Cocoa shim on macOS, platform stubs elsewhere.
if(APPLE)
    target_sources(hotcwap PRIVATE
        objc/window_cocoa.m
        vulkan/vulkan_mac.c
    )
    set_source_files_properties(objc/window_cocoa.m PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(hotcwap PUBLIC
        "-framework Cocoa"
        "-framework AppKit"
        "-framework QuartzCore"
        "-framework Metal"
    )
elseif(WIN32)
    target_sources(hotcwap PRIVATE window/window.c)
elseif(UNIX)
    target_sources(hotcwap PRIVATE window/window_linux.c)
endif()

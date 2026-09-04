cmake_minimum_required(VERSION 3.28)
project(hotcwap C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(APPLE)
    enable_language(OBJC)
    set(CMAKE_OBJC_STANDARD 23)
    set(CMAKE_OBJC_STANDARD_REQUIRED ON)
endif()

# Release: dead-code stripping so unused symbols cost zero bytes in artifacts.
# (Duplicated per repo for standalone builds; harmless when built in-tree.)
if(APPLE)
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    add_link_options($<$<CONFIG:Release>:-Wl,-dead_strip>)
elseif(UNIX)
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()

if(NOT TARGET hotcwap)
# The OS-stable layer: hot-reload machinery + the OS-agnostic window.
# A window never hot-updates — it is owned by the OS — so it lives here,
# next to the loader that persists across module swaps.
add_library(hotcwap STATIC
    app/application.c
    hot/hot.c
    hot/hot_trampoline.c
    hot/hot_retire.c
    hot/manifest.c
    hot/vk_loader.c
    vulkan/vulkan.c
)
target_include_directories(hotcwap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(hotcwap PRIVATE -Wall -Wextra -Werror)

# Vulkan headers: prefer find_package, fall back to brew --prefix
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND AND APPLE)
    execute_process(COMMAND brew --prefix OUTPUT_VARIABLE BREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(BREW_PREFIX AND EXISTS "${BREW_PREFIX}/include/vulkan/vulkan.h")
        set(Vulkan_INCLUDE_DIRS "${BREW_PREFIX}/include")
        set(Vulkan_FOUND TRUE)
    endif()
endif()
if(Vulkan_FOUND)
    target_include_directories(hotcwap PUBLIC ${Vulkan_INCLUDE_DIRS})
endif()

# Dynamic module sample: compiled into runtime dylib
add_library(vk_module MODULE hot/vk_module.c)
target_include_directories(vk_module PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(vk_module PRIVATE -Wall -Wextra -Werror)
if(Vulkan_FOUND)
    target_include_directories(vk_module PUBLIC ${Vulkan_INCLUDE_DIRS})
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
endif() # NOT TARGET hotcwap

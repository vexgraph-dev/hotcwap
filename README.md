# hotcwap, by Vex, truly.

Zero-downtime dynamic module hot-reloading and persistent OS windowing.

A play on the term **hot swap** — `hotcwap` is an infrastructure runtime designed to reload compiled C23 dynamic libraries in real-time without restarting the process, losing application state, or destroying the native operating system window.

In conventional game architectures, window management and simulation loops are tightly tangled. If a module crashes, reloads, or reconfigures, the window flickers, the graphics device is destroyed, and the event pump restarts. 

`hotcwap` enforces a strict architectural division: **a window belongs to the operating system and Thread 0, while simulation and graphics logic belong to reloadable modules.** By holding the window handle stable in `hotcwap`, modules and shaders can be swapped seamlessly on the fly.

---

## Key Architecture & Strengths

* **OS Window Decoupling**: Thread 0 hosts the native platform window (`AppKit` / Cocoa on macOS; X11/Wayland on Linux). The display link, event pump, and surface layer persist indefinitely across module reloads.
* **Vulkan CAMetalLayer Bridge**: Directly connects native Cocoa windows to MoltenVK / Vulkan swapchains via hardware-accelerated `CAMetalLayer` surfaces (`objc/window_cocoa.m`).
* **Microsecond Dynamic Reloader**: Monitors file manifests and filesystem timestamps (`hot/manifest.c`, `hot/hot.c`) to detect newly built dynamic libraries (`.dylib`), swap function pointer dispatch tables, and rebind entry points with zero frame interruption.
* **Vulkan GPA Loader**: Integrated `vkGetInstanceProcAddr` dynamic loader (`hot/vk_loader.c`) that extracts Vulkan symbols dynamically without requiring hard linkage to external loader stubs.
* **Ultra-Low Latency Event Pump**: Decoupled polling for keyboard, mouse, and touch events at 1000Hz resolution.

---

## Workspace Integration & How to Use It

`hotcwap` sits at Layer 2 in the `@vexgraph-dev` vertical integration stack, depending directly on `vexspoke` and serving downstream UI frameworks (`darling`) and application binaries (`vexgraph`):

```
workspace/
├── cmake-build-debug/           # Out-of-tree CMake build artifacts & staged SPVs
├── projects/                    # Vertically integrated subsystem repositories
│   ├── vexspoke/                # Bedrock C23 platform runtime (Layer 1)
│   ├── hotcwap/                 # Dynamic hot-reloading & native OS windowing (this library)
│   │   └── src/                 # Window abstraction, AppKit Cocoa bridge, loader
│   ├── darling/                 # Retained-mode UI nodes & Vulkan render passes (Layer 3)
│   ├── api-haven/               # Telemetry schemas & Discord webhook transmitters (Layer 4)
│   └── [other projects connecting to each other go here]
├── CMakeLists.txt               # Umbrella workspace orchestrator
└── preferences.md               # Engine architectural style preferences (Rules 1–n)
```

### 1. In-Tree Integration (Subdirectory)
When integrated inside an umbrella workspace:

```cmake
# In your top-level CMakeLists.txt
add_subdirectory(projects/hotcwap)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE hotcwap vexspoke)
```

### 2. Standalone Integration (FetchContent Seam)
When building standalone or in downstream projects:

```cmake
if(NOT TARGET hotcwap)
    include(FetchContent)
    FetchContent_Declare(
        hotcwap
        GIT_REPOSITORY https://github.com/vexgraph-dev/hotcwap.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(hotcwap)
endif()

target_link_libraries(my_app PRIVATE hotcwap)
```

---

## What's in this repo

* **`window/window.h/.c`** — Platform-agnostic window abstraction: creation, sizing, fullscreen toggles, input event dispatch, and title management.
* **`window/window_linux.c`** — Linux X11/Wayland display backend.
* **`objc/window_cocoa.m`** — Native macOS AppKit implementation: `NSWindow`, `NSView`, and `CAMetalLayer` creation with Retina backing scale handling and subpixel event mapping.
* **`hot/hot.h/.c`** — Dynamic module reloader: `dlopen`/`dlsym` lifecycle wrappers and runtime state preservation.
* **`hot/manifest.h/.c`** — Dynamic file manifest tracker and change detector.
* **`hot/vk_loader.c`** — Dynamic MoltenVK/Vulkan symbol loader and GPA function table generator.
* **`hot/vk_module.c`** — Hot-reloadable Vulkan pipeline module bindings.
* **`main/vk_test.c`** & **`tests/window_test.c`** — Verification test harnesses for Cocoa window creation, event polling, and dynamic library swapping.

---

## Requirements

* C23 compiler (Clang with `-std=gnu23`).
* macOS (AppKit, Cocoa, QuartzCore, Metal) or Linux (X11).
* CMake $\ge$ 4.3.
* Vulkan SDK / MoltenVK headers.

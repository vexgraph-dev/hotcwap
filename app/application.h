#ifndef HOT_APP_APPLICATION_H
#define HOT_APP_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "hot/hot.h"
#include "hot/spv_watch.h"
#include "thread/thread.h"
#include "window/window.h"

// app/application.h — Executable-level manifest, runtime orchestrator & window registry.
//
// Supports CLI, TUI, and GUI execution modes:
//   - CLI: Headless tool / daemon (0 windows, no graphics overhead)
//   - TUI: Terminal User Interface (console I/O + terminal tick)
//   - GUI: Windowed graphical application (Cocoa + Vulkan + presentation worker)
//
// Ownership law: Application REGISTERS windows, never destroys them during
// steady state. Application_free frees the Application runtime struct.

#define APP_MAX_WINDOWS 16
#define APP_MAX_NAME 64
#define APP_MAX_VERSION 16
#define APP_MAX_ICON_PATH 512

typedef enum AppMode {
    APP_MODE_AUTO = 0, // Auto-detect: GUI if windows registered, CLI if 0 windows
    APP_MODE_CLI,      // Headless / command-line tool (no window, no graphics)
    APP_MODE_TUI,      // Terminal User Interface (interactive console)
    APP_MODE_GUI,      // Windowed graphical application (Vulkan + OS window)
} AppMode;

typedef struct Application Application;

typedef int  (*AppRunFn)(Application *self, void *userdata);
typedef void (*AppTickFn)(Application *self, double dt, void *userdata);
typedef void (*AppHotReloadFn)(Application *self, uint32_t loaded, void *userdata);

struct Application {
    char name[APP_MAX_NAME];               // app name (default "anti")
    char author[APP_MAX_NAME];             // author / studio (default "")
    char version[APP_MAX_VERSION];         // version string, e.g. "1.2.3"
    char iconPath[APP_MAX_ICON_PATH];      // icon path reference (default "")
    Window *windows[APP_MAX_WINDOWS];      // registered top-level windows
    uint32_t window_count;                 // used slots in windows[]
    AppMode mode;                          // execution mode (default APP_MODE_AUTO)
    _Atomic bool running;                  // runtime active flag
    Thread *presentWorker;                 // background presentation thread (GUI mode)
    HotModule *hot;                        // dynamic module watcher
    SpvWatch *spvWatch;                    // SPIR-V shader watcher
    _Atomic uint32_t fps;                  // live telemetry: FPS
    _Atomic uint32_t frametimeUs;          // live telemetry: frametime (microseconds)
    AppRunFn runHandler;                   // custom run override (nullable)
    void *runUserdata;                     // userdata for runHandler
    AppTickFn tickFn;                      // per-frame tick callback (nullable)
    void *tickUserdata;                    // userdata for tickFn
    AppHotReloadFn hotReloadFn;            // hot-reload callback (nullable)
    void *hotReloadUserdata;               // userdata for hotReloadFn
};

// --- Subsystem bootstrap & shutdown ---
bool Application_init(void);
void Application_shutdown(void);

// --- Overloaded constructors (the Window chooser idiom) ---
//
//   Application()                          -> defaults ("anti", no windows)
//   Application("name")                    -> named
//   Application("name", "author", "1.0.0") -> full identity
//
// Strings are copied in (fixed storage, zero steady-state malloc).
// Getters return pointers into internal storage, stable until the next set.
Application *Application_0(void);
Application *Application_1(const char *name);
Application *Application_3(const char *name, const char *author, const char *version);

#define APPLICATION_CHOOSER(_0, _1, _2, _3, NAME, ...) NAME

#define Application(...) APPLICATION_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Application_3, Application_2, Application_1, Application_0 \
)(__VA_ARGS__)

// Free the Application. Registered windows are untouched (OS-owned).
void Application_free(Application *self);

// --- Execution & Runtime ---
int  Application_run(Application *self);
void Application_start(Application *self);
void Application_stop(Application *self);
bool Application_isRunning(const Application *self);
bool Application_tick(Application *self, double dt);

// --- Mode ---
void    Application_setMode(Application *self, AppMode mode);
AppMode Application_getMode(const Application *self);

// --- Lifecycle callbacks ---
void Application_setRunHandler(Application *self, AppRunFn fn, void *userdata);
void Application_onTick(Application *self, AppTickFn fn, void *userdata);
void Application_onHotReload(Application *self, AppHotReloadFn fn, void *userdata);

// --- Telemetry ---
uint32_t   Application_getFps(const Application *self);
uint32_t   Application_getFrametimeUs(const Application *self);
HotModule *Application_getHot(const Application *self);
SpvWatch  *Application_getSpvWatch(const Application *self);

// --- Identity: symmetric setters / getters (Rule 24) ---
void        Application_setName(Application *self, const char *name);
const char *Application_getName(const Application *self);
void        Application_setAuthor(Application *self, const char *author);
const char *Application_getAuthor(const Application *self);
void        Application_setVersion(Application *self, const char *version);
const char *Application_getVersion(const Application *self);
void        Application_setIconPath(Application *self, const char *iconPath);
const char *Application_getIconPath(const Application *self);

// --- Window registry (multiwindow) ---
// Register a live window. False on NULL, duplicate, or full registry.
bool      Application_addWindow(Application *self, Window *win);
// Unregister a window (swap-remove, order not preserved). False if absent.
bool      Application_removeWindow(Application *self, Window *win);
// Window at index, or NULL when out of range.
Window   *Application_getWindow(const Application *self, uint32_t index);
// Number of registered windows.
uint32_t  Application_getWindowCount(const Application *self);
// Copy registry into out[] (up to cap), returns entries written.
uint32_t  Application_getWindows(const Application *self, Window **out, uint32_t cap);

#endif

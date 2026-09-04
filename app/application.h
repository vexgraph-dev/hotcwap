#ifndef HOT_APP_APPLICATION_H
#define HOT_APP_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "window/window.h"

// app/application.h — Executable-level manifest + window registry.
//
// HotManifest describes one .dylib; WindowDesc describes one window.
// Application describes the executable itself: its identity (name, author,
// version, icon) and the set of live top-level windows. It is the truth
// about the running program that L3 mains read and L4 backends get told.
//
// Ownership law: Application REGISTERS windows, never destroys them.
// Windows are OS-owned (same law as the loader) — Application_free frees
// only the Application itself.

#define APP_MAX_WINDOWS 16
#define APP_MAX_NAME 64
#define APP_MAX_VERSION 16
#define APP_MAX_ICON_PATH 512

typedef struct Application {
    char name[APP_MAX_NAME];               // app name (default "anti")
    char author[APP_MAX_NAME];             // author / studio (default "")
    char version[APP_MAX_VERSION];         // version string, e.g. "1.2.3"
    char iconPath[APP_MAX_ICON_PATH];      // icon path reference (default "")
    Window *windows[APP_MAX_WINDOWS];      // registered top-level windows
    uint32_t window_count;                 // used slots in windows[]
} Application;

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

// --- Identity: symmetric setters / getters (Rule 24) ---
void Application_setName(Application *self, const char *name);
const char *Application_getName(const Application *self);
void Application_setAuthor(Application *self, const char *author);
const char *Application_getAuthor(const Application *self);
void Application_setVersion(Application *self, const char *version);
const char *Application_getVersion(const Application *self);
void Application_setIconPath(Application *self, const char *iconPath);
const char *Application_getIconPath(const Application *self);

// --- Window registry (multiwindow) ---
// Register a live window. False on NULL, duplicate, or full registry.
bool Application_addWindow(Application *self, Window *win);
// Unregister a window (swap-remove, order not preserved). False if absent.
bool Application_removeWindow(Application *self, Window *win);
// Window at index, or NULL when out of range.
Window *Application_getWindow(const Application *self, uint32_t index);
// Number of registered windows.
uint32_t Application_getWindowCount(const Application *self);
// Copy registry into out[] (up to cap), returns entries written.
uint32_t Application_getWindows(const Application *self, Window **out, uint32_t cap);

#endif

#include "app/application.h"

#include <stdlib.h>
#include <string.h>

#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Application (app/application.c)
 * LEVEL: L2 — Behavior (executable identity + window registry API)
 * ============================================================================
 * The truth about the running executable: name, author, version, icon —
 * the manifest of it all, above per-module HotManifests and per-window
 * WindowDescs. Owns a fixed-cap registry of live top-level windows
 * (multiwindow); registers only, never destroys (OS owns windows).
 *
 * STRUCT FIELDS (Mirroring app/application.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   char name[APP_MAX_NAME];               // app name (default "anti")
 *   char author[APP_MAX_NAME];             // author / studio (default "")
 *   char version[APP_MAX_VERSION];         // version string, e.g. "1.2.3"
 *   char iconPath[APP_MAX_ICON_PATH];      // icon path reference (default "")
 *   Window *windows[APP_MAX_WINDOWS];      // registered windows (max 16)
 *   uint32_t window_count;                 // used slots in windows[]
 *
 * PRIVATE HELPERS: None.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Application()                        : Application_0()
 *   - Application(name)                    : Application_1(name)
 *   - Application(name, author, version)   : Application_3(name, author, version)
 *
 * Core Functions:
 *   - Application_free(self)
 *   - Application_addWindow(self, win)
 *   - Application_removeWindow(self, win)
 *
 * Setters:
 *   - Application_setName(self, name)
 *   - Application_setAuthor(self, author)
 *   - Application_setVersion(self, version)
 *   - Application_setIconPath(self, iconPath)
 *
 * Getters:
 *   - Application_getName(self)
 *   - Application_getAuthor(self)
 *   - Application_getVersion(self)
 *   - Application_getIconPath(self)
 *   - Application_getWindow(self, index)
 *   - Application_getWindowCount(self)
 *   - Application_getWindows(self, out, cap)
 * ============================================================================
 */

// CONSTRUCTORS
Application *Application_0(void) {
    Application *self = (Application*) calloc(1, sizeof(Application));
    if (!self) return NULL;
    strncpy((*self).name, "anti", APP_MAX_NAME - 1);
    return self;
}

Application *Application_1(const char *name) {
    Application *self = Application_0();
    if (!self) return NULL;
    Application_setName(self, name);
    return self;
}

Application *Application_3(const char *name, const char *author, const char *version) {
    Application *self = Application_0();
    if (!self) return NULL;
    Application_setName(self, name);
    Application_setAuthor(self, author);
    Application_setVersion(self, version);
    return self;
}

// CORE FUNCTIONS
void Application_free(Application *self) {
    if (!self) return;
    free(self);
}

bool Application_addWindow(Application *self, Window *win) {
    if (!self || !win) return false;
    for (uint32_t i = 0; i < (*self).window_count; i++)
        if ((*self).windows[i] == win) return false;
    if ((*self).window_count >= APP_MAX_WINDOWS) return false;
    (*self).windows[(*self).window_count++] = win;
    return true;
}

bool Application_removeWindow(Application *self, Window *win) {
    if (!self || !win) return false;
    for (uint32_t i = 0; i < (*self).window_count; i++) {
        if ((*self).windows[i] == win) {
            (*self).windows[i] = (*self).windows[--(*self).window_count];
            (*self).windows[(*self).window_count] = NULL;
            return true;
        }
    }
    return false;
}

// SETTERS
void Application_setName(Application *self, const char *name) {
    if (!self || !name) return;
    strncpy((*self).name, name, APP_MAX_NAME - 1);
    (*self).name[APP_MAX_NAME - 1] = '\0';
}

void Application_setAuthor(Application *self, const char *author) {
    if (!self || !author) return;
    strncpy((*self).author, author, APP_MAX_NAME - 1);
    (*self).author[APP_MAX_NAME - 1] = '\0';
}

void Application_setVersion(Application *self, const char *version) {
    if (!self || !version) return;
    strncpy((*self).version, version, APP_MAX_VERSION - 1);
    (*self).version[APP_MAX_VERSION - 1] = '\0';
}

void Application_setIconPath(Application *self, const char *iconPath) {
    if (!self || !iconPath) return;
    strncpy((*self).iconPath, iconPath, APP_MAX_ICON_PATH - 1);
    (*self).iconPath[APP_MAX_ICON_PATH - 1] = '\0';
}

// GETTERS
const char *Application_getName(const Application *self) {
    if (!self) return NULL;
    return (*self).name;
}

const char *Application_getAuthor(const Application *self) {
    if (!self) return NULL;
    return (*self).author;
}

const char *Application_getVersion(const Application *self) {
    if (!self) return NULL;
    return (*self).version;
}

const char *Application_getIconPath(const Application *self) {
    if (!self) return NULL;
    return (*self).iconPath;
}

Window *Application_getWindow(const Application *self, uint32_t index) {
    if (!self) return NULL;
    if (index >= (*self).window_count) return NULL;
    return (*self).windows[index];
}

uint32_t Application_getWindowCount(const Application *self) {
    if (!self) return 0;
    return (*self).window_count;
}

uint32_t Application_getWindows(const Application *self, Window **out, uint32_t cap) {
    if (!self || !out) return 0;
    uint32_t n = (*self).window_count;
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; i++)
        out[i] = (*self).windows[i];
    return n;
}

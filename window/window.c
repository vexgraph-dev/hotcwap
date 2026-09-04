// window.c — Win32 backend for the Window API.
//
// The annotations (@PlatformExclusive, @Draft, @Intention, @Incomplete) are
// real markers defined in src/annotation/*.h — C has no annotation feature,
// so they are macros that expand to static asserts embedding the annotation
// text in compiler diagnostics / debug info.
//
// Every function below is an @Incomplete stub returning a safe default. The
// real implementations will use User32: CreateWindowEx / DefWindowProc,
// ShowWindow / SetWindowTextA / SetWindowPos / SetWindowDisplayAffinity,
// and a GetMessage/PeekMessage-TranslateMessage-DispatchMessage event loop.

#include "window/window.h"
#include "annotation/draft.h"
#include "annotation/incomplete.h"
#include "annotation/intention.h"
#include "annotation/platform_exclusive.h"

#if defined(_WIN32)

;;PLATFORM_EXCLUSIVE("Windows")
;;INTENTION("Fills the Window API seam (window.h) on Windows so the engine can be built there; mirrors the legacy windowsWindow.java.")
;;DRAFT

;;;;INCOMPLETE // CreateWindowEx; returns nullptr until implemented.
Window *Window_create(const char *title, int width, int height) {
    (void) title;
    (void) width;
    (void) height;
    return nullptr;
}

;;INCOMPLETE // DestroyWindow; no-op until implemented.
void Window_destroy(Window *window) {
    (void) window;
}

;;INCOMPLETE // Poll WM_QUIT; false until implemented.
bool Window_shouldClose(Window *window) {
    (void) window;
    return false;
}

;;INCOMPLETE // PeekMessage/TranslateMessage/DispatchMessage loop; no-op until implemented.
void Window_pollEvents(void) {
}

;;INCOMPLETE // DXGI swap interval; no-op until implemented.
void Window_setPresentMode(Window *window, int mode) {
    (void) window;
    (void) mode;
}

int Window_getPresentMode(const Window *window) {
    (void) window;
    return WINDOW_PRESENT_FIFO;
}

;;INCOMPLETE // Layered window alpha; no-op until implemented.
void Window_setTransparent(Window *window, bool transparent) {
    (void) window;
    (void) transparent;
}

bool Window_isTransparent(const Window *window) {
    (void) window;
    return false;
}

uint64_t Window_renderGeneration(const Window *window) {
    (void) window;
    return 0;
}

;;INCOMPLETE // Content root slot; no-op until implemented.
void Window_setContainer(Window *window, Panel *root) {
    (void) window;
    (void) root;
}

Panel *Window_getContainer(const Window *window) {
    (void) window;
    return nullptr;
}

;;INCOMPLETE // Input kill switch; no-op until implemented.
void Window_setEnabled(Window *window, bool enabled) {
    (void) window;
    (void) enabled;
}

bool Window_isEnabled(const Window *window) {
    (void) window;
    return true;
}

;;INCOMPLETE // SetWindowTextA; no-op until implemented.
void Window_setTitle(Window *window, const char *title) {
    (void) window;
    (void) title;
}

;;INCOMPLETE // SetWindowPos; no-op until implemented.
void Window_setSize(Window *window, int width, int height) {
    (void) window;
    (void) width;
    (void) height;
}

;;INCOMPLETE // SetWindowPos; no-op until implemented.
void Window_setLocation(Window *window, int x, int y) {
    (void) window;
    (void) x;
    (void) y;
}

void Window_getContentOrigin(const Window *window, int *outX, int *outY) {
    if (outX) *outX = 0;
    if (outY) *outY = 0;
}

void Window_setResizeRenderHook(Window *window, WindowResizeRenderFn fn, void *userdata) {
    (void) window;
    (void) fn;
    (void) userdata;
}

uint32_t Window_getMonitorId(const Window *window) {
    (void) window;
    return 0;
}

;;INCOMPLETE // GetWindowRect top-left; zeros until implemented.
void Window_getLocation(const Window *window, int *outX, int *outY) {
    if (outX) *outX = 0;
    if (outY) *outY = 0;
}

;;INCOMPLETE // CenterWindow; no-op until implemented.
void Window_center(Window *window) {
    (void) window;
}

;;INCOMPLETE // ShowWindow(SW_SHOW)/ShowWindow(SW_HIDE); no-op until implemented.
void Window_setVisible(Window *window, bool visible) {
    (void) window;
    (void) visible;
}

;;INCOMPLETE // GWL_STYLE WS_THICKFRAME; false until implemented.
bool Window_isResizable(Window *window) {
    (void) window;
    return false;
}

;;INCOMPLETE // SetWindowLong(GWL_STYLE, WS_THICKFRAME); no-op until implemented.
void Window_setResizable(Window *window, bool resizable) {
    (void) window;
    (void) resizable;
}

;;INCOMPLETE // GWL_STYLE WS_SYSMENU; false until implemented.
bool Window_isClosable(Window *window) {
    (void) window;
    return false;
}

;;INCOMPLETE // SetWindowLong(GWL_STYLE, WS_SYSMENU); no-op until implemented.
void Window_setClosable(Window *window, bool closable) {
    (void) window;
    (void) closable;
}

;;INCOMPLETE // GWL_STYLE WS_MINIMIZEBOX; false until implemented.
bool Window_isMiniaturizable(Window *window) {
    (void) window;
    return false;
}

;;INCOMPLETE // SetWindowLong(GWL_STYLE, WS_MINIMIZEBOX); no-op until implemented.
void Window_setMiniaturizable(Window *window, bool miniaturizable) {
    (void) window;
    (void) miniaturizable;
}

;;INCOMPLETE // WS_MAXIMIZEBOX semantics; no-op until implemented.
void Window_setFullscreenButton(Window *window, bool enabled) {
    (void) window;
    (void) enabled;
}

;;INCOMPLETE // Fullscreen/borderless chrome switch; no-op until implemented.
void Window_setUndecorated(Window *window, int mode) {
    (void) window;
    (void) mode;
}

;;INCOMPLETE // ShowWindow(SW_MINIMIZE); no-op until implemented.
void Window_minimize(Window *window) {
    (void) window;
}

;;INCOMPLETE // ShowWindow(SW_RESTORE); no-op until implemented.
void Window_restore(Window *window) {
    (void) window;
}

;;INCOMPLETE // IsIconic; false until implemented.
bool Window_isMinimized(Window *window) {
    (void) window;
    return false;
}

;;INCOMPLETE // GWL_STYLE WS_MAXIMIZE; false until implemented.
bool Window_isFullscreen(Window *window) {
    (void) window;
    return false;
}

;;INCOMPLETE // ShowWindow(SW_MAXIMIZE) vs SW_RESTORE; no-op until implemented.
void Window_setFullscreen(Window *window, bool fullscreen) {
    (void) window;
    (void) fullscreen;
}

;;INCOMPLETE // Window setFullscreen wrapper; no-op until implemented.
void Window_toggleFullscreen(Window *window) {
    (void) window;
}

;;INCOMPLETE // SetWindowDisplayAffinity(WDA_MONITOR/WDA_NONE); no-op until implemented.
void Window_setDRM(Window *window, bool enabled) {
    (void) window;
    (void) enabled;
}

;;INCOMPLETE // AdjustWindowRectEx + GetWindowRect; no-op until implemented.
void Window_setMinSize(Window *window, int width, int height) {
    (void) window;
    (void) width;
    (void) height;
}

;;INCOMPLETE // AdjustWindowRectEx + GetWindowRect; no-op until implemented.
void Window_setMaxSize(Window *window, int width, int height) {
    (void) window;
    (void) width;
    (void) height;
}

#endif
/*
 * Copyright (C) 2013, Igalia S.L.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef  WaylandCompositor_h
#define  WaylandCompositor_h

#include <gtk/gtk.h>

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cairo.h>
#include <cairo-gl.h>

#include <wtf/HashMap.h>

#include "GLContext.h"
#include "IntSize.h"
#include "RefPtrCairo.h"

namespace WebCore {

class WaylandCompositor;

// Nested compositor display
struct NestedDisplay {
    GdkDisplay* gdkDisplay;               // Gdk display
    struct wl_display* wlDisplay;         // Main Wayland display
    struct wl_display* childDisplay;      // Nested display
    EGLDisplay eglDisplay;                // EGL display
    EGLConfig eglConfig;                  // EGL configuration
    EGLContext eglCtx;                    // EGL context
    cairo_device_t* eglDevice;            // EGL cairo device
    struct wl_global* wlGlobal;           // Wayland display global
    struct wl_global* wkgtkGlobal;        // Wayland webkitgtk interface global
    GSource* eventSource;                 // Display event source
};

struct NestedBuffer {
    struct wl_resource* resource;
    struct wl_signal destroySignal;
    struct wl_listener destroyListener;
    uint32_t busyCount;
};

struct NestedBufferReference {
    struct NestedBuffer* buffer;
    struct wl_listener destroyListener;
};

// Nested Wayland compositor surface
struct NestedSurface {
    WaylandCompositor* compositor;        // Nested compositor instance
    struct NestedBuffer* buffer;          // Last attached buffer (pending buffer)
    GLuint texture;                       // GL texture for the surface
    EGLImageKHR* image;                   // EGL Image for texture
    cairo_surface_t* cairoSurface;        // Cairo surface for GL texture
    struct wl_list frameCallbackList;     // Pending frame callback list
    GtkWidget* widget;                    // widget associated with this surface
    struct wl_listener bufferDestroyListener; // Pending buffer destroy listener
    struct NestedBufferReference bufferRef;   // Current buffer
    struct wl_list link;
};

// List of pending frame callbacks on a nested surface
struct NestedFrameCallback {
    struct wl_resource* resource;
    struct wl_list link;
};

class WaylandCompositor {
public:
    static WaylandCompositor* instance();
    virtual ~WaylandCompositor();

    // WebKit integration
    void addWidget(GtkWidget*);
    void removeWidget(GtkWidget*);
    int getWidgetId(GtkWidget*);
    cairo_surface_t* cairoSurfaceForWidget(GtkWidget*);

    // Wayland Webkit extension interface
    static void wkgtkSetSurfaceForWidget(struct wl_client*, struct wl_resource*, struct wl_resource*, uint32_t);

    // Wayland compositor interface
    static void createSurface(struct wl_client*, wl_resource*, uint32_t);
    static void createRegion(struct wl_client*, wl_resource*, uint32_t);

    // Wayland surface interface
    static void surfaceDestroy(struct wl_client*, struct wl_resource*);
    static void surfaceAttach(struct wl_client*, struct wl_resource*, struct wl_resource*, int32_t, int32_t);
    static void surfaceDamage(struct wl_client*, struct wl_resource*, int32_t, int32_t, int32_t, int32_t);
    static void surfaceFrame(struct wl_client*, struct wl_resource*, uint32_t);
    static void surfaceSetOpaqueRegion(struct wl_client*, struct wl_resource*, struct wl_resource*);
    static void surfaceSetInputRegion(struct wl_client*, struct wl_resource*, struct wl_resource*);
    static void surfaceCommit(struct wl_client*, struct wl_resource*);
    static void surfaceSetBufferTransform(struct wl_client*, struct wl_resource*, int32_t);
    static void surfaceSetBufferScale(struct wl_client*, struct wl_resource*, int32_t);

private:
    WaylandCompositor();

    // Nested compositor display initialization
    static bool supportsRequiredExtensions(EGLDisplay);
    static bool initEGL(struct NestedDisplay*);
    static void shutdownEGL(struct NestedDisplay*);
    static struct NestedDisplay* createNestedDisplay();
    static void destroyNestedDisplay(struct NestedDisplay*);
    bool initialize();

    // Global binding
    static void wkgtkBind(struct wl_client*, void*, uint32_t, uint32_t);
    static void compositorBind(struct wl_client*, void*, uint32_t, uint32_t);

    // Widget/Surface mapping
    static struct NestedSurface* getSurfaceForWidget(WaylandCompositor*, GtkWidget*);
    static void setSurfaceForWidget(WaylandCompositor*, GtkWidget*, struct NestedSurface*);
    static GtkWidget* getWidgetById(WaylandCompositor*, int);

    // Surface management
    static void doDestroyNestedSurface(struct NestedSurface*);
    static void destroyNestedSurface(struct wl_resource*);
    static void destroyNestedFrameCallback(struct wl_resource*);

    // Buffer management
    static void nestedBufferDestroyHandler(struct wl_listener*, void*);
    static struct NestedBuffer* nestedBufferFromResource(struct wl_resource*);
    static void surfaceHandlePendingBufferDestroy(struct wl_listener*, void*);
    static void nestedBufferReference(struct NestedBufferReference*, struct NestedBuffer*);
    static void nestedBufferReferenceHandleDestroy(struct wl_listener*, void*);

    // Global instance
    static WaylandCompositor* m_instance;

    struct NestedDisplay* m_display;
    HashMap<GtkWidget*, struct NestedSurface*> m_widgetHashMap;
    struct wl_list m_surfaces;
};

} // namespace WebCore

#endif

#endif // WaylandCompositor_h

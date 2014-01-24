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

#include "GLContext.h"
#include "IntSize.h"
#include "RefPtrCairo.h"

namespace WebCore {

struct NestedSurface;
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
  struct wl_global* wlGlobal;           // Wayland global
  GSource* eventSource;                 // Display event source
};

// Nested Wayland compositor surface
struct NestedSurface {
  WaylandCompositor* compositor;        // Nested compositor instance
  struct wl_resource* bufferResource;   // Last attached buffer
  GLuint texture;                       // GL texture for the surface
  EGLImageKHR* image;                   // EGL Image for texture
  cairo_surface_t* cairoSurface;        // Cairo surface for GL texture
  struct wl_list frameCallbackList;     // Pending frame callback list
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
    void nextFrame();
    cairo_surface_t* cairoSurfaceForWidget(GtkWidget*);

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

    // Wayland callbacks
    static void compositorBind(struct wl_client*, void*, uint32_t, uint32_t);
    static void doDestroyNestedSurface(struct NestedSurface*);
    static void destroyNestedSurface (struct wl_resource*);
    static void destroyNestedFrameCallback (struct wl_resource*);

    static WaylandCompositor* m_instance;

    struct NestedDisplay* m_display;
    struct NestedSurface* m_surface;
    GtkWidget* m_widget;
};

} // namespace WebCore

#endif

#endif // WaylandCompositor_h

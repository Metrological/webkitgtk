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

#ifndef WaylandCompositor_h
#define WaylandCompositor_h

#include <gtk/gtk.h>

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <memory>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wtf/HashMap.h>

namespace WebCore {

class WaylandCompositor;

// Nested Wayland compositor surface
struct NestedSurface {
    NestedSurface(WaylandCompositor*);
    virtual ~NestedSurface();

    WaylandCompositor* compositor;        // Nested compositor instance
    struct NestedBuffer* buffer;          // Last attached buffer (pending buffer)
    struct wl_list frameCallbackList;     // Pending frame callback list
    GtkWidget* widget;                    // widget associated with this surface
    struct wl_listener bufferDestroyListener; // Pending buffer destroy listener
    struct wl_list link;

    struct BufferReference {
        struct NestedBuffer* buffer;
        struct wl_listener destroyListener;
    } bufferRef; // Current buffer.
};

struct NestedBuffer {
    NestedBuffer(struct wl_resource* resource)
        : resource(resource)
        , busyCount(0)
    {
        wl_signal_init(&destroySignal);
        destroyListener.notify = destroyHandler;
        wl_resource_add_destroy_listener(resource, &destroyListener);
    }

    static struct NestedBuffer* fromResource(struct wl_resource*);
    static void reference(NestedSurface::BufferReference*, struct NestedBuffer*);
    static void destroyHandler(struct wl_listener*, void*);

    struct wl_resource* resource;
    struct wl_signal destroySignal;
    struct wl_listener destroyListener;
    uint32_t busyCount;
};

// Nested compositor display
struct NestedDisplay {
    ~NestedDisplay();

    GdkDisplay* gdkDisplay;               // Gdk display
    struct wl_display* wlDisplay;         // Main Wayland display
    EGLDisplay eglDisplay;

    struct wl_display* childDisplay;      // Nested display
    struct wl_global* wlGlobal;           // Wayland display global
    struct wl_global* wkgtkGlobal;        // Wayland webkitgtk interface global
    GSource* eventSource;                 // Display event source
};

// List of pending frame callbacks on a nested surface
struct NestedFrameCallback {
    NestedFrameCallback(struct wl_resource* resource)
        : resource(resource)
    { }

    struct wl_resource* resource;
    struct wl_list link;
};

class WaylandCompositor {
public:
    enum CompositorType {
        EGL,
    };

    static WaylandCompositor* instance();
    virtual ~WaylandCompositor();

    // WebKit integration
    void addWidget(GtkWidget*);
    void removeWidget(GtkWidget*);

    void setSurfaceForWidget(struct wl_client*, struct wl_resource*, uint32_t);

    void createSurface(struct wl_client*, uint32_t);

    virtual void attachSurface(NestedSurface*, struct wl_client*, struct wl_resource*, int32_t, int32_t) = 0;
    virtual void requestFrame(NestedSurface*, struct wl_client*, uint32_t) = 0;
    virtual void commitSurface(NestedSurface*, struct wl_client*) = 0;

    struct RenderingContext {
        RenderingContext(CompositorType type)
            : type(type)
        { }

        CompositorType type;
    };
    virtual void render(RenderingContext&) = 0;

    EGLDisplay eglDisplay() { return m_display->eglDisplay; }

protected:
    WaylandCompositor();

    // Nested compositor display initialization
    virtual bool initialize();
    virtual bool initializeEGL() = 0;

    virtual NestedSurface* createNestedSurface() = 0;

    std::unique_ptr<NestedDisplay> m_display;
    HashMap<GtkWidget*, NestedSurface*> m_widgetHashMap;
    struct wl_list m_surfaces;
};

} // namespace WebCore

#endif

#endif // WaylandCompositor_h

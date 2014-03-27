/*
 * Copyright (C) 2013 Igalia S.L.
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

#include "config.h"
#include "WaylandCompositor.h"

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include "WaylandCompositorEGL.h"
#include "WaylandDisplayEventSource.h"
#include "WaylandWebkitGtkServerProtocol.h"

#include <gdk/gdkwayland.h>

namespace WebCore {

// Wayland EGL functions
#if !defined(PFNEGLBINDWAYLANDDISPLAYWL)
typedef EGLBoolean (EGLAPIENTRYP PFNEGLBINDWAYLANDDISPLAYWL)(EGLDisplay, struct wl_display*);
#endif

#if !defined(PFNEGLUNBINDWAYLANDDISPLAYWL)
typedef EGLBoolean (EGLAPIENTRYP PFNEGLUNBINDWAYLANDDISPLAYWL)(EGLDisplay, struct wl_display*);
#endif

static PFNEGLBINDWAYLANDDISPLAYWL eglBindDisplay = nullptr;
static PFNEGLUNBINDWAYLANDDISPLAYWL eglUnbindDisplay = nullptr;

NestedBuffer* NestedBuffer::fromResource(struct wl_resource* resource)
{
    struct wl_listener* listener = wl_resource_get_destroy_listener(resource, destroyHandler);
    if (listener)
        return wl_container_of(listener, static_cast<NestedBuffer*>(nullptr), destroyListener);

    return new NestedBuffer(resource);
}

void NestedBuffer::reference(NestedSurface::BufferReference* ref, NestedBuffer* buffer)
{
    if (buffer == ref->buffer)
        return;

    if (ref->buffer) {
        ref->buffer->busyCount--;
        if (ref->buffer->busyCount == 0)
            wl_resource_queue_event(ref->buffer->resource, WL_BUFFER_RELEASE);
        wl_list_remove(&ref->destroyListener.link);
    }

    if (buffer) {
        buffer->busyCount++;
        wl_signal_add(&buffer->destroySignal, &ref->destroyListener);
        ref->destroyListener.notify = [](struct wl_listener* listener, void* data) {
            NestedSurface::BufferReference* ref = nullptr;
            ref = wl_container_of(listener, ref, destroyListener);
            ref->buffer = nullptr;
        };
    }

    ref->buffer = buffer;
}

void NestedBuffer::destroyHandler(struct wl_listener* listener, void* data)
{
    NestedBuffer* buffer = nullptr;
    buffer = wl_container_of(listener, buffer, destroyListener);
    wl_signal_emit(&buffer->destroySignal, buffer);
    delete buffer;
}

NestedSurface::NestedSurface(WaylandCompositor* compositor)
    : compositor(compositor)
{
    wl_list_init(&frameCallbackList);

    wl_list_init(&bufferDestroyListener.link);
    bufferDestroyListener.notify = [](struct wl_listener* listener, void* data) {
        NestedSurface* surface = nullptr;
        surface = wl_container_of(listener, surface, bufferDestroyListener);
        surface->buffer = nullptr;
    };

    bufferRef.buffer = nullptr;
    wl_list_init(&bufferRef.destroyListener.link);
}

NestedSurface::~NestedSurface()
{
    // Destroy pending frame callbacks
    NestedFrameCallback *cb, *next;
    wl_list_for_each_safe(cb, next, &frameCallbackList, link)
        wl_resource_destroy(cb->resource);
    wl_list_init(&frameCallbackList);

    // Release current buffer
    NestedBuffer::reference(&bufferRef, nullptr);

    // Unlink this surface from the compositor's surface list
    wl_list_remove(&link);
}

NestedDisplay::~NestedDisplay()
{
    if (eventSource)
        g_source_remove(g_source_get_id(eventSource));
    if (wkgtkGlobal)
        wl_global_destroy(wkgtkGlobal);
    if (wlGlobal)
        wl_global_destroy(wlGlobal);
    if (childDisplay)
        wl_display_destroy(childDisplay);
}

static const struct wl_surface_interface surfaceInterface = {
    // destroy
    [](struct wl_client*, struct wl_resource* resource)
    {
        wl_resource_destroy(resource);
    },

    // attach
    [](struct wl_client* client, struct wl_resource* resource, struct wl_resource* bufferResource, int32_t sx, int32_t sy)
    {
        NestedSurface* surface = static_cast<NestedSurface*>(wl_resource_get_user_data(resource));
        if (surface)
            surface->compositor->attachSurface(surface, client, bufferResource, sx, sy);
    },

    // damage
    [](struct wl_client*, struct wl_resource*, int32_t, int32_t, int32_t, int32_t)
    {
        // FIXME: Try to use damage information to improve painting
    },

    // frame
    [](struct wl_client* client, struct wl_resource* resource, uint32_t id)
    {
        NestedSurface* surface = static_cast<NestedSurface*>(wl_resource_get_user_data(resource));
        if (surface)
            surface->compositor->requestFrame(surface, client, id);
    },

    // set_opaque_region
    [](struct wl_client*, struct wl_resource*, struct wl_resource*) { },

    // set_input_region
    [](struct wl_client*, struct wl_resource*, struct wl_resource*) { },

    // commit
    [](struct wl_client* client, struct wl_resource* resource)
    {
        NestedSurface* surface = static_cast<NestedSurface*>(wl_resource_get_user_data(resource));
        if (surface)
            surface->compositor->commitSurface(surface, client);
    },

    // set_buffer_transform
    [](struct wl_client*, struct wl_resource*, int32_t) { },

    // set_buffer_scale
    [](struct wl_client*, struct wl_resource*, int32_t) { }
};

static const struct wl_compositor_interface compositorInterface = {
    // create_surface
    [](struct wl_client* client, struct wl_resource* resource, uint32_t id)
    {
        WaylandCompositor* compositor = static_cast<WaylandCompositor*>(wl_resource_get_user_data(resource));
        compositor->createSurface(client, id);
    },

    // create_region
    [](struct wl_client*, struct wl_resource*, uint32_t) { }
};

static const struct wl_wkgtk_interface wkgtkInterface = {
    // set_surface_for_widget
    [](struct wl_client* client, struct wl_resource* resource, struct wl_resource* surfaceResource, uint32_t id)
    {
        WaylandCompositor* compositor = static_cast<WaylandCompositor*>(wl_resource_get_user_data(resource));
        compositor->setSurfaceForWidget(client, surfaceResource, id);
    }
};

WaylandCompositor* WaylandCompositor::instance()
{
    static WaylandCompositor* compositor = nullptr;
    if (compositor)
        return compositor;

    compositor = new WaylandCompositorEGL();
    if (!compositor->initialize()) {
        delete compositor;
        return nullptr;
    }

    return compositor;
}

WaylandCompositor::WaylandCompositor()
    : m_display(nullptr)
{
    wl_list_init(&m_surfaces);
}

WaylandCompositor::~WaylandCompositor()
{
    NestedSurface *surface, *next;
    wl_list_for_each_safe(surface, next, &m_surfaces, link)
        delete surface;
    wl_list_init(&m_surfaces);
}

bool WaylandCompositor::initialize()
{
    GdkDisplay* gdkDisplay = gdk_display_manager_get_default_display(gdk_display_manager_get());
    struct wl_display* wlDisplay = gdk_wayland_display_get_wl_display(gdkDisplay);
    if (!wlDisplay)
        return false;

    // Create the nested display
    m_display = std::make_unique<NestedDisplay>();
    m_display->gdkDisplay = gdkDisplay;
    m_display->wlDisplay = wlDisplay;

    m_display->childDisplay = wl_display_create();
    if (!m_display->childDisplay) {
        g_warning("Nested Wayland compositor could not create display object.");
        return false;
    }

    if (wl_display_add_socket(m_display->childDisplay, "webkitgtk-wayland-compositor-socket") != 0) {
        g_warning("Nested Wayland compositor could not create display socket");
        return false;
    }

    // Bind the compositor to this display
    m_display->wlGlobal = wl_global_create(m_display->childDisplay, &wl_compositor_interface, wl_compositor_interface.version, this,
        [](struct wl_client* client, void* data, uint32_t version, uint32_t id) {
            WaylandCompositor* compositor = static_cast<WaylandCompositor*>(data);
            struct wl_resource* resource = wl_resource_create(client, &wl_compositor_interface, std::min(static_cast<int>(version), 3), id);
            wl_resource_set_implementation(resource, &compositorInterface, compositor, nullptr);
        }
    );
    if (!m_display->wlGlobal) {
        g_warning("Nested Wayland compositor could not register display global");
        return false;
    }

    // Bind the webkitgtk protocol extension
    m_display->wkgtkGlobal = wl_global_create(m_display->childDisplay, &wl_wkgtk_interface, 1, this,
        [](struct wl_client* client, void* data, uint32_t version, uint32_t id) {
            WaylandCompositor* compositor = static_cast<WaylandCompositor*>(data);;
            struct wl_resource* resource = wl_resource_create(client, &wl_wkgtk_interface, 1, id);
            wl_resource_set_implementation(resource, &wkgtkInterface, compositor, nullptr);
        }
    );
    if (!m_display->wkgtkGlobal) {
        g_warning("Nested Wayland compositor could not register webkitgtk global");
        return false;
    }

    if (!initializeEGL()) {
        g_warning("Nested Wayland compositor could not initialize EGL.");
        return false;
    }

    // Make sure we have the required Wayland EGL extension
    const char* extensions = eglQueryString(m_display->eglDisplay, EGL_EXTENSIONS);
    if (!strstr(extensions, "EGL_WL_bind_wayland_display")) {
        g_warning("Nested Wayland compositor requires EGL_WL_bind_wayland_display extension.\n");
        return false;
    }

    // Map required EGL functions
    eglBindDisplay = (PFNEGLBINDWAYLANDDISPLAYWL) eglGetProcAddress("eglBindWaylandDisplayWL");
    eglUnbindDisplay = (PFNEGLUNBINDWAYLANDDISPLAYWL) eglGetProcAddress("eglUnbindWaylandDisplayWL");
    if (!eglBindDisplay || !eglUnbindDisplay) {
        g_warning("EGL supports EGL_WL_bind_wayland_display extension but does not provide extension functions.");
        return false;
    }

    if (!eglBindDisplay(m_display->eglDisplay, m_display->childDisplay)) {
        g_warning("Nested Wayland compositor could not bind nested display");
        return false;
    }

    // Handle our display events through GLib's main loop
    m_display->eventSource = WaylandDisplayEventSource::createDisplayEventSource(m_display->childDisplay);

    return true;
}

void WaylandCompositor::addWidget(GtkWidget* widget)
{
    static int nextWidgetId = 0;
    g_object_set_data(G_OBJECT(widget), "wayland-compositor-widget-id", GINT_TO_POINTER(++nextWidgetId));
    m_widgetHashMap.set(widget, nullptr);
}

void WaylandCompositor::removeWidget(GtkWidget* widget)
{
    NestedSurface* surface = m_widgetHashMap.get(widget);
    if (surface)
        surface->widget = nullptr;

    int widgetId = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "wayland-compositor-widget-id"));
    if (widgetId) {
        m_widgetHashMap.remove(widget);
        g_object_steal_data(G_OBJECT(widget), "wayland-compositor-widget-id");
    }
}

void WaylandCompositor::setSurfaceForWidget(struct wl_client*, struct wl_resource* surfaceResource, uint32_t id)
{
    NestedSurface* surface = static_cast<NestedSurface*>(wl_resource_get_user_data(surfaceResource));
    for (const auto& widget : m_widgetHashMap.keys()) {
        if (id == static_cast<guint>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "wayland-compositor-widget-id")))) {
            // Associate the new surface with the widget, the client is responsible
            // for destroying any previous surface created for this widget
            m_widgetHashMap.set(widget, surface);
            surface->widget = widget;
            break;
        }
    }
}

void WaylandCompositor::createSurface(struct wl_client* client, uint32_t id)
{
    NestedSurface* surface = createNestedSurface();

    // Create the surface resource
    struct wl_resource* resource = wl_resource_create(client, &wl_surface_interface, 1, id);
    wl_resource_set_implementation(resource, &surfaceInterface, surface,
        [](struct wl_resource* resource)
        {
            NestedSurface* surface = static_cast<NestedSurface*>(wl_resource_get_user_data(resource));
            delete surface;
        }
    );

    wl_list_insert(m_surfaces.prev, &surface->link);
}

} // namespace WebCore

#endif // PLATFORM(WAYLAND)

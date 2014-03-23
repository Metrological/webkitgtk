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
#include "WaylandDisplay.h"

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <wtf/OwnPtr.h>

#include <wayland-egl.h>
#include <EGL/egl.h>
#include <string.h>

namespace WebCore {

const struct wl_registry_listener WaylandDisplay::m_registryListener = {
    // global
    [](void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t)
    {
        WaylandDisplay* display = static_cast<WaylandDisplay*>(data);
        if (strcmp(interface, "wl_compositor") == 0)
            display->m_compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
        else if (strcmp(interface, "wl_wkgtk") == 0)
            display->m_wkgtk = static_cast<struct wl_wkgtk*>(wl_registry_bind(registry, name, &wl_wkgtk_interface, 1));
    },

    // global_remove
    [](void*, struct wl_registry*, uint32_t)
    {
        // FIXME: if this can happen without the UI Process getting shut down we should probably
        // destroy our cached display instance
    }
};

WaylandDisplay* WaylandDisplay::instance()
{
    WaylandDisplay* display = nullptr;
    if (display)
        return display;

    struct wl_display* wlDisplay = wl_display_connect("webkitgtk-wayland-compositor-socket");
    if (!wlDisplay)
        return nullptr;

    display= new WaylandDisplay(wlDisplay);
    return display;
}

WaylandDisplay::WaylandDisplay(struct wl_display* wlDisplay)
    : m_display(wlDisplay)
    , m_registry(nullptr)
    , m_compositor(nullptr)
    , m_wkgtk(nullptr)
{
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &m_registryListener, this);
    wl_display_roundtrip(m_display);
}

PassOwnPtr<WaylandSurface> WaylandDisplay::createSurface(int width, int height, int widgetId)
{
    struct wl_surface* surface = wl_compositor_create_surface(m_compositor);
    EGLNativeWindowType native = wl_egl_window_create(surface, width, height);
    OwnPtr<WaylandSurface> wlSurface = WaylandSurface::create(surface, native);
    wl_wkgtk_set_surface_for_widget(m_wkgtk, surface, widgetId);
    return wlSurface.release();
}

void WaylandDisplay::destroySurface(WaylandSurface* surface)
{
    if (surface) {
        wl_egl_window_destroy(surface->nativeWindowHandle());
        wl_surface_destroy(surface->surface());
    }
}

}

#endif

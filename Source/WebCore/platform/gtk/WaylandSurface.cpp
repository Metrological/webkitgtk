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
#include "WaylandSurface.h"

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include "WaylandDisplay.h"

namespace WebCore {

void WaylandSurface::frameCallback(void* data, struct wl_callback* callback, uint32_t time)
{
    // FIXME: Looks like this is never called. Why?
    if (callback)
        wl_callback_destroy(callback);
}

static const struct wl_callback_listener frameListener = {
    WaylandSurface::frameCallback
};

void WaylandSurface::requestFrame()
{
    struct wl_callback* wlCallback = wl_surface_frame(m_surface);
    wl_callback_add_listener(wlCallback, &frameListener, this);
}

PassOwnPtr<WaylandSurface> WaylandSurface::create(struct wl_surface* surface, EGLNativeWindowType native)
{
    return adoptPtr(new WaylandSurface(surface, native));
}

WaylandSurface::WaylandSurface(struct wl_surface* surface, EGLNativeWindowType native)
    : m_surface(surface)
    , m_native(native)
{

}

WaylandSurface::~WaylandSurface()
{
    WaylandDisplay *display = WaylandDisplay::instance();
    if (display)
        display->destroySurface(this);
}

}

#endif

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

#include "GLContextEGL.h"
#include "WaylandDisplay.h"

namespace WebCore {

PassOwnPtr<WaylandSurface> WaylandSurface::create(EGLContext eglContext, EGLSurface eglSurface, struct wl_surface* wlSurface, EGLNativeWindowType nativeWindow)
{
    return adoptPtr(new WaylandSurface(eglContext, eglSurface, wlSurface, nativeWindow));
}

WaylandSurface::WaylandSurface(EGLContext eglContext, EGLSurface eglSurface, struct wl_surface* wlSurface, EGLNativeWindowType nativeWindow)
    : m_eglContext(eglContext)
    , m_eglSurface(eglSurface)
    , m_wlSurface(wlSurface)
    , m_frameCallback(nullptr)
    , m_nativeWindow(nativeWindow)
{
}

WaylandSurface::~WaylandSurface()
{
    WaylandDisplay *display = WaylandDisplay::instance();
    if (display)
        display->destroySurface(this);
}

PassOwnPtr<GLContextEGL> WaylandSurface::createGLContext()
{
    return GLContextEGL::adoptWindowContext(m_eglContext, m_eglSurface);
}

static const struct wl_callback_listener frameListener = {
    WaylandSurface::frameCallback
};

void WaylandSurface::requestFrame()
{
    m_frameCallback = wl_surface_frame(m_wlSurface);
    wl_callback_add_listener(m_frameCallback, &frameListener, this);
}

void WaylandSurface::frameCallback(void* data, struct wl_callback*, uint32_t)
{
    WaylandSurface* surface = static_cast<WaylandSurface*>(data);
    surface->m_frameCallback = nullptr;
}

}

#endif

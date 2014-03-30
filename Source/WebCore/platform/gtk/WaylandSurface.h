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

#ifndef  WaylandSurface_h
#define  WaylandSurface_h

#include <gdk/gdk.h>

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <wtf/PassOwnPtr.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>

namespace WebCore {

class GLContextEGL;

class WaylandSurface
{
public:
    static PassOwnPtr<WaylandSurface> create(EGLContext eglContext, EGLSurface eglSurface, struct wl_surface* wlSurface, EGLNativeWindowType nativeWindow);
    {
        return adoptPtr(new WaylandSurface(eglContext, eglSurface, wlSurface, nativeWindow));
    }
    virtual ~WaylandSurface();

    // Surface interface
    struct wl_surface* surface() { return m_wlSurface; }
    EGLNativeWindowType nativeWindowHandle() { return m_nativeWindow; }
    EGLSurface eglSurface() { return m_eglSurface; }

    PassOwnPtr<GLContextEGL> createGLContext();

    void requestFrame();
    static void frameCallback(void*, struct wl_callback*, uint32_t);

private:
    WaylandSurface(EGLContext, EGLSurface, struct wl_surface*, EGLNativeWindowType);

    EGLContext m_eglContext;
    EGLSurface m_eglSurface;
    struct wl_surface* m_wlSurface;
    struct wl_callback* m_frameCallback;
    EGLNativeWindowType m_nativeWindow;
};

}

#endif

#endif


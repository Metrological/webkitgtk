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

#ifndef  WaylandDisplay_h
#define  WaylandDisplay_h

#include <gdk/gdk.h>

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <wtf/PassOwnPtr.h>

#include <wayland-client.h>

#include "WaylandSurface.h"
#include "WaylandWebkitGtkClientProtocol.h"

namespace WebCore {

class WaylandDisplay
{
public:
    static WaylandDisplay* instance();
    static struct wl_display* nativeDisplay();

    // Display interface
    PassOwnPtr<WaylandSurface> createSurface(int, int, int);
    void destroySurface(WaylandSurface*);

    // Wayland registry listener interface callbacks
    static void registryHandleGlobal(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
    static void registryHandleGlobalRemove(void*, struct wl_registry*, uint32_t);

private:
    WaylandDisplay();
    ~WaylandDisplay();

    struct wl_registry* m_registry;
    struct wl_compositor* m_compositor;
    struct wl_wkgtk* m_wkgtk;

    EGLDisplay m_eglDisplay;
    EGLConfig m_eglConfig;
    EGLContext m_eglContext;

    static WaylandDisplay* m_instance;
    static struct wl_display* m_nativeDisplay;
};

}

#endif

#endif

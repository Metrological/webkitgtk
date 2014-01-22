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
#include "WaylandDisplayEventSource.h"

#if PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND) && !defined(GTK_API_VERSION_2)

#include <wayland-server.h>

namespace WebCore {

typedef struct _WaylandGlibEventSource {
    GSource source;
    GPollFD pfd;
    struct wl_display* display;
} WaylandGlibEventSource;

gboolean WaylandDisplayEventSource::prepare(GSource* base, gint* timeout)
{
    *timeout = -1;
    return FALSE;
}

gboolean WaylandDisplayEventSource::check(GSource* base)
{
    WaylandGlibEventSource* source = reinterpret_cast<WaylandGlibEventSource*>(base);
    return source->pfd.revents;
}

gboolean WaylandDisplayEventSource::dispatch(GSource* base, GSourceFunc callback, gpointer data)
{
    WaylandGlibEventSource* source = reinterpret_cast<WaylandGlibEventSource*>(base);
    struct wl_display* display = source->display;
    struct wl_event_loop* loop;

    if (source->pfd.revents & G_IO_IN) {
        loop = wl_display_get_event_loop(display);
        wl_event_loop_dispatch(loop, -1);
        wl_display_flush_clients(display);
        source->pfd.revents = 0;
    }

    if (source->pfd.revents & (G_IO_ERR | G_IO_HUP))
        g_warning("Wayland Display Event Source: lost connection to nested Wayland compositor");

    return TRUE;
}

void WaylandDisplayEventSource::finalize(GSource* source)
{
}

static GSourceFuncs waylandGlibSourceFuncs = {
    WaylandDisplayEventSource::prepare,
    WaylandDisplayEventSource::check,
    WaylandDisplayEventSource::dispatch,
    WaylandDisplayEventSource::finalize,
    0,
    0
};

GSource* WaylandDisplayEventSource::createDisplayEventSource(struct wl_display* display)
{
    GSource* source = g_source_new(&waylandGlibSourceFuncs, sizeof(WaylandGlibEventSource));
    gchar* name = g_strdup_printf("Nested Wayland compositor display event source");
    g_source_set_name(source, name);
    g_free(name);

    WaylandGlibEventSource* wlSource = reinterpret_cast<WaylandGlibEventSource*>(source);
    wlSource->display = display;
    struct wl_event_loop* loop = wl_display_get_event_loop(display);
    wlSource->pfd.fd = wl_event_loop_get_fd(loop);
    wlSource->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
    g_source_add_poll(source, &wlSource->pfd);

    g_source_set_priority(source, GDK_PRIORITY_EVENTS);
    g_source_set_can_recurse(source, TRUE);
    g_source_attach(source, NULL);

    return source;
}

}

#endif

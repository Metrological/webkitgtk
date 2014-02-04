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

#if !defined(PFNEGLQUERYWAYLANDBUFFERWL)
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYWAYLANDBUFFERWL)(EGLDisplay, struct wl_resource*, EGLint, EGLint*);
#endif

#if !defined(EGL_WAYLAND_BUFFER_WL)
#define	EGL_WAYLAND_BUFFER_WL 0x31D5
#endif

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC eglImageTargetTexture2d = 0;
static PFNEGLCREATEIMAGEKHRPROC            eglCreateImage          = 0;
static PFNEGLDESTROYIMAGEKHRPROC           eglDestroyImage         = 0;
static PFNEGLBINDWAYLANDDISPLAYWL          eglBindDisplay          = 0;
static PFNEGLUNBINDWAYLANDDISPLAYWL        eglUnbindDisplay        = 0;
static PFNEGLQUERYWAYLANDBUFFERWL          eglQueryBuffer          = 0;

#if USE(OPENGL_ES_2)
static const EGLenum gGLAPI = EGL_OPENGL_ES_API;
#else
static const EGLenum gGLAPI = EGL_OPENGL_API;
#endif

void WaylandCompositor::nestedBufferReferenceHandleDestroy(struct wl_listener* listener, void* data)
{
    struct NestedBufferReference* ref = 0;
    ref = wl_container_of(listener, ref, destroyListener);
    ref->buffer = 0;
}

void WaylandCompositor::nestedBufferReference(struct NestedBufferReference* ref, struct NestedBuffer* buffer)
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
        ref->destroyListener.notify = nestedBufferReferenceHandleDestroy;
    }

    ref->buffer = buffer;
}

void WaylandCompositor::surfaceHandlePendingBufferDestroy(struct wl_listener* listener, void* data)
{
    struct NestedSurface* surface = 0;
    surface = wl_container_of(listener, surface, bufferDestroyListener);
    surface->buffer = 0;
}

void WaylandCompositor::nestedBufferDestroyHandler(struct wl_listener* listener, void* data)
{
    struct NestedBuffer* buffer = 0;
    buffer = wl_container_of(listener, buffer, destroyListener);
    wl_signal_emit(&buffer->destroySignal, buffer);
    g_free(buffer);
}

struct NestedBuffer* WaylandCompositor::nestedBufferFromResource(struct wl_resource* resource)
{
    struct NestedBuffer* buffer;
    struct wl_listener* listener;

    listener = wl_resource_get_destroy_listener(resource, nestedBufferDestroyHandler);
    if (listener)
        return wl_container_of(listener, (struct NestedBuffer*)0, destroyListener);

    buffer = g_new0(struct NestedBuffer, 1);
    buffer->resource = resource;
    wl_signal_init(&buffer->destroySignal);
    buffer->destroyListener.notify = nestedBufferDestroyHandler;
    wl_resource_add_destroy_listener(resource, &buffer->destroyListener);

    return buffer;
}

bool WaylandCompositor::supportsRequiredExtensions(EGLDisplay eglDisplay)
{
    // Make sure we have the required Wayland EGL extension
    const char* extensions = eglQueryString(eglDisplay, EGL_EXTENSIONS);
    if (!strstr(extensions, "EGL_WL_bind_wayland_display")) {
        g_warning("Nested Wayland compositor requires EGL_WL_bind_wayland_display extension.\n");
        return false;
    }

    // Map required EGL functions
    eglBindDisplay = (PFNEGLBINDWAYLANDDISPLAYWL) eglGetProcAddress("eglBindWaylandDisplayWL");
    eglUnbindDisplay = (PFNEGLUNBINDWAYLANDDISPLAYWL) eglGetProcAddress("eglUnbindWaylandDisplayWL");
    if (!eglBindDisplay || !eglUnbindDisplay) {
        g_warning("Nested Wayland compositor requires eglBindWaylandDisplayWL.");
        return false;
    }

    eglCreateImage = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImage = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    if (!eglCreateImage || !eglDestroyImage) {
        g_warning("Nested Wayland compositor requires eglCreateImageKHR.");
        return false;
    }

    eglQueryBuffer = (PFNEGLQUERYWAYLANDBUFFERWL) eglGetProcAddress("eglQueryWaylandBufferWL");
    if (!eglQueryBuffer) {
        g_warning("Nested Wayland compositor requires eglQueryWaylandBufferWL.");
        return false;
    }

    eglImageTargetTexture2d =	(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!eglImageTargetTexture2d) {
        g_warning("Nested Wayland compositor requires glEGLImageTargetTexture.");
        return false;
    }

    return true;
}

void WaylandCompositor::shutdownEGL(struct NestedDisplay* d)
{
    if (d->eglDevice) {
        cairo_device_destroy(d->eglDevice);
        d->eglDevice = 0;
    }
    if (d->eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(d->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (d->eglCtx != EGL_NO_CONTEXT) {
            eglDestroyContext(d->eglDisplay, d->eglCtx);
            d->eglCtx = EGL_NO_CONTEXT;
        }
        d->eglDisplay = EGL_NO_DISPLAY;
    }
}

bool WaylandCompositor::initEGL(struct NestedDisplay* d)
{
    EGLint n;

    // FIXME: we probably want to get the EGL configuration from GLContextEGL
    static const EGLint contextAttribs[] = {
#if USE(OPENGL_ES_2)
        EGL_CONTEXT_CLIENT_VERSION, 2,
#endif
        EGL_NONE
    };
    static const EGLint cfgAttribs[] = {
#if USE(OPENGL_ES_2)
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
#else
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
#endif
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_STENCIL_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
    };

    if ((d->eglDisplay = eglGetDisplay(d->wlDisplay)) == EGL_NO_DISPLAY)
        return false;

    if (eglInitialize(d->eglDisplay, 0, 0) == EGL_FALSE)
        return false;

    if (!supportsRequiredExtensions(d->eglDisplay))
        return false;

    if (eglBindAPI(gGLAPI) == EGL_FALSE)
        return false;

    if (!eglChooseConfig(d->eglDisplay, cfgAttribs, &d->eglConfig, 1, &n) || n != 1)
        return false;

    d->eglCtx = eglCreateContext(d->eglDisplay, d->eglConfig, EGL_NO_CONTEXT, contextAttribs);
    if (d->eglCtx == EGL_NO_CONTEXT)
        return false;

    if (!eglMakeCurrent(d->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, d->eglCtx))
        return false;

    d->eglDevice = cairo_egl_device_create(d->eglDisplay, d->eglCtx);
    if (cairo_device_status(d->eglDevice) != CAIRO_STATUS_SUCCESS)
        return false;

    return true;
}

struct NestedDisplay* WaylandCompositor::createNestedDisplay()
{
    struct NestedDisplay* display = g_new0(struct NestedDisplay, 1);
    GdkDisplay* gdkDisplay = gdk_display_manager_get_default_display(gdk_display_manager_get());
    display->gdkDisplay = gdkDisplay;
    display->wlDisplay = gdk_wayland_display_get_wl_display(gdkDisplay);

    if (!display->wlDisplay || !initEGL(display)) {
        shutdownEGL(display);
        g_free(display);
        return 0;
    }

    return display;
}

void WaylandCompositor::destroyNestedDisplay(struct NestedDisplay* display)
{
    if (display->childDisplay) {
        if (display->eventSource)
            g_source_remove(g_source_get_id(display->eventSource));
        if (display->wlGlobal)
            wl_global_destroy(display->wlGlobal);
        if (display->wkgtkGlobal)
            wl_global_destroy(display->wkgtkGlobal);
        wl_display_destroy(display->childDisplay);
    }
    shutdownEGL(display);
    g_free(display);
}

void WaylandCompositor::destroyNestedFrameCallback(struct wl_resource* resource)
{
    struct NestedFrameCallback* callback = static_cast<NestedFrameCallback*>(wl_resource_get_user_data(resource));
    wl_list_remove(&callback->link);
    g_free(callback);
}

void WaylandCompositor::surfaceDestroy(struct wl_client* client, struct wl_resource* resource)
{
    wl_resource_destroy(resource);
}

void WaylandCompositor::surfaceAttach(struct wl_client* client, struct wl_resource* resource, struct wl_resource* bufferResource, int32_t sx, int32_t sy)
{
    struct NestedSurface* surface = static_cast<struct NestedSurface*>(wl_resource_get_user_data(resource));

    EGLint format;
    WaylandCompositor* compositor = surface->compositor;
    if (!eglQueryBuffer(compositor->m_display->eglDisplay, bufferResource, EGL_TEXTURE_FORMAT, &format))
        return;
    if (format != EGL_TEXTURE_RGB && format != EGL_TEXTURE_RGBA)
        return;

    // Remove references to any previous pending buffer for this surface
    if (surface->buffer) {
        surface->buffer = 0;
        wl_list_remove(&surface->bufferDestroyListener.link);
    }

    // Make the new buffer the current pending buffer
    if (bufferResource) {
        surface->buffer = nestedBufferFromResource(bufferResource);
        wl_signal_add(&surface->buffer->destroySignal, &surface->bufferDestroyListener);
    }
}

void WaylandCompositor::surfaceDamage(struct wl_client* client, struct wl_resource* resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
    // FIXME: Try to use damage information to improve painting
}

void WaylandCompositor::surfaceFrame(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    struct NestedSurface* surface = static_cast<struct NestedSurface*>(wl_resource_get_user_data(resource));
    if (!surface)
        return;

    // Queue frame callback until we are done with the current frame and are ready to process the next one
    // (see nextFrame)
    struct NestedFrameCallback* callback = g_new0(struct NestedFrameCallback, 1);
    callback->resource = wl_resource_create(client, &wl_callback_interface, 1, id);
    wl_resource_set_implementation(callback->resource, 0, callback, destroyNestedFrameCallback);
    wl_list_insert(surface->frameCallbackList.prev, &callback->link);
}

void WaylandCompositor::surfaceSetOpaqueRegion(struct wl_client* client, struct wl_resource* resource, struct wl_resource* regionResource)
{

}

void WaylandCompositor::surfaceSetInputRegion(struct wl_client* client, struct wl_resource* resource, struct wl_resource* regionResource)
{

}

void WaylandCompositor::surfaceCommit(struct wl_client* client, struct wl_resource* resource)
{
    struct NestedSurface* surface = static_cast<struct NestedSurface*>(wl_resource_get_user_data(resource));
    if (!surface)
        return;

    WaylandCompositor* compositor = surface->compositor;

    // Destroy any existing EGLImage for this surface
    if (surface->image != EGL_NO_IMAGE_KHR)
        eglDestroyImage(compositor->m_display->eglDisplay, surface->image);

    // Destroy any existing cairo surface for this surface
    if (surface->cairoSurface) {
        cairo_surface_destroy(surface->cairoSurface);
        surface->cairoSurface = 0;
    }

    // Make the pending buffer current 
    nestedBufferReference(&surface->bufferRef, surface->buffer);

    // Create a new EGLImage from the last buffer attached to this surface
    EGLDisplay eglDisplay = compositor->m_display->eglDisplay;
    surface->image = static_cast<EGLImageKHR*>(eglCreateImage(eglDisplay, EGL_NO_CONTEXT, EGL_WAYLAND_BUFFER_WL, surface->buffer->resource, 0));
    if (surface->image == EGL_NO_IMAGE_KHR)
        return;

    // Bind the surface texture to the new EGLImage
    glBindTexture(GL_TEXTURE_2D, surface->texture);
    eglImageTargetTexture2d(GL_TEXTURE_2D, surface->image);

    // Create a new cairo surface associated with the surface texture
    int width, height;
    eglQueryBuffer(eglDisplay, surface->buffer->resource, EGL_WIDTH, &width);
    eglQueryBuffer(eglDisplay, surface->buffer->resource, EGL_HEIGHT, &height);
    cairo_device_t* device = compositor->m_display->eglDevice;
    surface->cairoSurface = cairo_gl_surface_create_for_texture(device, CAIRO_CONTENT_COLOR_ALPHA, surface->texture, width, height);
    cairo_surface_mark_dirty (surface->cairoSurface); // FIXME: Why do we need this?

    // We are done with this buffer
    if (surface->buffer) {
        wl_list_remove(&surface->bufferDestroyListener.link);
        surface->buffer = 0;
    }

    // Redraw the widget backed by this surface
    if (surface->widget)
        gtk_widget_queue_draw(surface->widget);
}

void WaylandCompositor::surfaceSetBufferTransform(struct wl_client* client, struct wl_resource* resource, int32_t transform)
{

}

void WaylandCompositor::surfaceSetBufferScale(struct wl_client* client, struct wl_resource* resource, int32_t scale)
{

}

static const struct wl_surface_interface surfaceInterface = {
    WaylandCompositor::surfaceDestroy,
    WaylandCompositor::surfaceAttach,
    WaylandCompositor::surfaceDamage,
    WaylandCompositor::surfaceFrame,
    WaylandCompositor::surfaceSetOpaqueRegion,
    WaylandCompositor::surfaceSetInputRegion,
    WaylandCompositor::surfaceCommit,
    WaylandCompositor::surfaceSetBufferTransform,
    WaylandCompositor::surfaceSetBufferScale
};

void WaylandCompositor::doDestroyNestedSurface(struct NestedSurface* surface)
{
    // Destroy pending frame callbacks
    struct NestedFrameCallback *cb, *next;
    wl_list_for_each_safe(cb, next, &surface->frameCallbackList, link)
        wl_resource_destroy(cb->resource);
    wl_list_init(&surface->frameCallbackList);

    // Destroy EGLImage
    if (surface->image != EGL_NO_IMAGE_KHR) {
        eglDestroyImage(surface->compositor->m_display->eglDisplay, surface->image);
        surface->image = 0;
    }

    // Release current buffer
    nestedBufferReference(&surface->bufferRef, NULL);

    // Delete GL texture
    glDeleteTextures(1, &surface->texture);

    // Unlink this source from the compositor's surface list
    wl_list_remove(&surface->link);

    g_free(surface);
}

void WaylandCompositor::destroyNestedSurface(struct wl_resource* resource)
{
    struct NestedSurface* surface = static_cast<struct NestedSurface*>(wl_resource_get_user_data(resource));
    if (surface)
        doDestroyNestedSurface(surface);
}

void WaylandCompositor::createSurface(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{
    WaylandCompositor* compositor = static_cast<WaylandCompositor*>(wl_resource_get_user_data(resource));

    struct NestedSurface* surface = g_new0(struct NestedSurface, 1);
    surface->compositor = compositor;

    wl_list_init(&surface->frameCallbackList);
    surface->bufferDestroyListener.notify = surfaceHandlePendingBufferDestroy;

    // Create a GL texture to back this surface
    glGenTextures (1, &surface->texture);
    glBindTexture (GL_TEXTURE_2D, surface->texture);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Create the surface resource
    struct wl_resource* surfaceResource = wl_resource_create(client, &wl_surface_interface, 1, id);
    wl_resource_set_implementation(surfaceResource, &surfaceInterface, surface, destroyNestedSurface);

    wl_list_insert(compositor->m_surfaces.prev, &surface->link);
}

void WaylandCompositor::createRegion(struct wl_client* client, struct wl_resource* resource, uint32_t id)
{

}

static const struct wl_compositor_interface compositorInterface = {
    WaylandCompositor::createSurface,
    WaylandCompositor::createRegion
};

void WaylandCompositor::compositorBind(wl_client* client, void* data, uint32_t version, uint32_t id)
{
    WaylandCompositor* compositor = static_cast<WaylandCompositor*>(data);
    struct wl_resource* resource = wl_resource_create(client, &wl_compositor_interface, std::min(static_cast<int>(version), 3), id);
    wl_resource_set_implementation(resource, &compositorInterface, compositor, 0);
}

void WaylandCompositor::wkgtkSetSurfaceForWidget(struct wl_client* client, struct wl_resource* resource, struct wl_resource* surfaceResource, uint32_t id)
{
    WaylandCompositor* compositor = static_cast<WaylandCompositor*>(wl_resource_get_user_data(resource));
    GtkWidget* widget = getWidgetById(compositor, id);
    if (!widget)
        return;
    struct NestedSurface* surface = static_cast<struct NestedSurface*>(wl_resource_get_user_data(surfaceResource));
    setSurfaceForWidget(compositor, widget, surface);
}

static const struct wl_wkgtk_interface wkgtkInterface = {
    WaylandCompositor::wkgtkSetSurfaceForWidget,
};


void WaylandCompositor::wkgtkBind(struct wl_client* client, void* data, uint32_t version, uint32_t id)
{
    WaylandCompositor* compositor = static_cast<WaylandCompositor*>(data);;
    struct wl_resource* resource = wl_resource_create(client, &wl_wkgtk_interface, 1, id);
    wl_resource_set_implementation(resource, &wkgtkInterface, compositor, 0);
}

bool WaylandCompositor::initialize()
{
    // Create the nested display
    m_display = createNestedDisplay();
    if (!m_display)
        return false;

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
    m_display->wlGlobal = wl_global_create(m_display->childDisplay, &wl_compositor_interface, wl_compositor_interface.version, this, compositorBind);
    if (!m_display->wlGlobal) {
        g_warning("Nested Wayland compositor could not register display global");
        return false;
    }

    // Bind the webkitgtk protocol extension
    m_display->wkgtkGlobal = wl_global_create(m_display->childDisplay, &wl_wkgtk_interface, 1, this, wkgtkBind);
    if (!m_display->wkgtkGlobal) {
        g_warning("Nested Wayland compositor could not register webkitgtk global");
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

WaylandCompositor* WaylandCompositor::m_instance = 0;

WaylandCompositor* WaylandCompositor::instance()
{
    if (m_instance)
        return m_instance;

    WaylandCompositor* compositor = new WaylandCompositor();
    if (!compositor->initialize()) {
        delete compositor;
        return 0;
    }

    m_instance = compositor;
    return m_instance;
}

WaylandCompositor::WaylandCompositor()
    : m_display(0)
{
    wl_list_init(&m_surfaces);
}

WaylandCompositor::~WaylandCompositor()
{
    struct NestedSurface *surface, *next;
    wl_list_for_each_safe(surface, next, &m_surfaces, link)
        doDestroyNestedSurface(surface);
    wl_list_init(&m_surfaces);

    if (m_display)
        destroyNestedDisplay(m_display);
}

void WaylandCompositor::addWidget(GtkWidget* widget)
{
    static int nextWidgetId = 0;
    g_object_set_data(G_OBJECT(widget), "wayland-compositor-widget-id", GINT_TO_POINTER(++nextWidgetId));
    setSurfaceForWidget(this, widget, 0);
}

int WaylandCompositor::getWidgetId(GtkWidget* widget)
{
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "wayland-compositor-widget-id"));
}

struct NestedSurface* WaylandCompositor::getSurfaceForWidget(WaylandCompositor* compositor, GtkWidget* widget)
{
    return compositor->m_widgetHashMap.get(widget);
}

void WaylandCompositor::setSurfaceForWidget(WaylandCompositor* compositor, GtkWidget* widget, struct NestedSurface* surface)
{
    // Associate the new surface with the widget, the client is responsible
    // for destroying any previous surface created for this widget
    compositor->m_widgetHashMap.set(widget, surface);
    if (surface)
        surface->widget = widget;
}

GtkWidget* WaylandCompositor::getWidgetById(WaylandCompositor* compositor, int id)
{
    for (HashMap<GtkWidget*, struct NestedSurface*>::iterator it = compositor->m_widgetHashMap.begin(); it != compositor->m_widgetHashMap.end(); ++it) {
        GtkWidget* widget = it->key;
        if (id == GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "wayland-compositor-widget-id")))
            return widget;
    }
    return 0;
}

cairo_surface_t* WaylandCompositor::cairoSurfaceForWidget(GtkWidget* widget)
{
    struct NestedSurface* surface = getSurfaceForWidget(this, widget);
    return surface ? surface->cairoSurface : 0;
}

void WaylandCompositor::nextFrame(GtkWidget* widget)
{
    struct NestedSurface* surface = getSurfaceForWidget(this, widget);
    if (!surface)
        return;

    // Process frame callbacks for the surface
    struct NestedFrameCallback *nc, *next;
    wl_list_for_each_safe(nc, next, &surface->frameCallbackList, link) {
        wl_callback_send_done(nc->resource, 0);
        wl_resource_destroy(nc->resource);
    }

    wl_list_init(&surface->frameCallbackList);
    wl_display_flush_clients(m_display->childDisplay);
}

} // namespace WebCore

#endif // PLATFORM(WAYLAND)

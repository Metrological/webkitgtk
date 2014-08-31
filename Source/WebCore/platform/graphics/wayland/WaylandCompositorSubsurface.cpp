#include "config.h"
#include "WaylandCompositorSubsurface.h"

#if USE(EGL) && PLATFORM(WAYLAND)

#include <cstring>
#include <EGL/eglext.h>
#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>

#if !defined(PFNEGLQUERYWAYLANDBUFFERWL)
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYWAYLANDBUFFERWL)(EGLDisplay, struct wl_resource*, EGLint, EGLint*);
#endif

#ifndef EGL_WL_create_wayland_buffer_from_image
#define EGL_WL_create_wayland_buffer_from_image 1

#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI struct wl_buffer * EGLAPIENTRY eglCreateWaylandBufferFromImageWL(EGLDisplay dpy, EGLImageKHR image);
#endif
typedef struct wl_buffer * (EGLAPIENTRYP PFNEGLCREATEWAYLANDBUFFERFROMIMAGEWL) (EGLDisplay dpy, EGLImageKHR image);

#endif

#if !defined(EGL_WAYLAND_BUFFER_WL)
#define	EGL_WAYLAND_BUFFER_WL 0x31D5
#endif

static PFNEGLCREATEIMAGEKHRPROC eglCreateImage = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImage = nullptr;
static PFNEGLQUERYWAYLANDBUFFERWL eglQueryBuffer = nullptr;
static PFNEGLCREATEWAYLANDBUFFERFROMIMAGEWL eglCreateWaylandBufferFromImage = nullptr;

namespace WebCore {

struct NestedSurfaceSubS : NestedSurface {
    NestedSurfaceSubS(WaylandCompositor* compositor)
        : NestedSurface(compositor)
        , image(nullptr)
        , subsurfaceSurface(nullptr)
        , subsurface(nullptr)
    { }
    virtual ~NestedSurfaceSubS() { }

    virtual void setWidget(GtkWidget*) override;
    virtual NestedBuffer* createBuffer(struct wl_resource*) override;

    EGLImageKHR image;                   // EGL Image for texture
    struct wl_surface* subsurfaceSurface;
    struct wl_subsurface* subsurface;
};

struct NestedBufferSubS : NestedBuffer {
    NestedBufferSubS(struct wl_resource* resource)
        : NestedBuffer(resource)
        , parentBuffer(nullptr)
    {
    }

    struct wl_buffer* parentBuffer;
};

void NestedSurfaceSubS::setWidget(GtkWidget* widget)
{
    NestedSurface::setWidget(widget);

    WaylandCompositorSubsurface* subsurfaceCompositor = static_cast<WaylandCompositorSubsurface*>(compositor);
    subsurfaceSurface = wl_compositor_create_surface(subsurfaceCompositor->wl_compositor);
    wl_surface_set_user_data(subsurfaceSurface, gtk_widget_get_window(widget));
    subsurface = wl_subcompositor_get_subsurface(subsurfaceCompositor->wl_subcompositor,
        subsurfaceSurface, gdk_wayland_window_get_wl_surface(gtk_widget_get_window(widget)));
    
}

NestedBuffer* NestedSurfaceSubS::createBuffer(struct wl_resource* resource)
{
    return new NestedBufferSubS(resource);
}

const struct wl_registry_listener WaylandCompositorSubsurface::m_registryListener = {
    // global
    [](void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t)
    {
        WaylandCompositorSubsurface* compositor = static_cast<WaylandCompositorSubsurface*>(data);
        if (std::strcmp(interface, "wl_compositor") == 0)
            compositor->wl_compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 3));
        else if (std::strcmp(interface, "wl_subcompositor") == 0)
            compositor->wl_subcompositor = static_cast<struct wl_subcompositor*>(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    },

    // global_remove
    [](void*, struct wl_registry*, uint32_t)
    {
        // FIXME: if this can happen without the UIProcess getting shut down,
        // we should probably destroy our cached display interface.
    }
};

WaylandCompositorSubsurface::WaylandCompositorSubsurface()
{
}

void WaylandCompositorSubsurface::attachSurface(NestedSurface* surfaceBase, struct wl_client*, struct wl_resource* bufferResource, int32_t, int32_t)
{
    NestedSurfaceSubS* surface = static_cast<NestedSurfaceSubS*>(surfaceBase);

    // Remove references to any previous pending buffer for this surface.
    if (surface->buffer)
        wl_list_remove(&surface->bufferDestroyListener.link);
    surface->buffer = nullptr;

    // Make the new buffer the current pending buffer.
    if (bufferResource) {
        surface->buffer = NestedBuffer::fromResource(surface, bufferResource);
        wl_signal_add(&surface->buffer->destroySignal, &surface->bufferDestroyListener);
    }
}

void WaylandCompositorSubsurface::requestFrame(NestedSurface* surfaceBase, struct wl_client* client, uint32_t id)
{
    NestedSurfaceSubS* surface = static_cast<NestedSurfaceSubS*>(surfaceBase);

    NestedFrameCallback* callback = new NestedFrameCallback(wl_resource_create(client, &wl_callback_interface, 1, id));
    wl_resource_set_implementation(callback->resource, nullptr, callback,
        [](struct wl_resource* resource) {
            NestedFrameCallback* callback = static_cast<NestedFrameCallback*>(wl_resource_get_user_data(resource));
            wl_list_remove(&callback->link);
            delete callback;
        }
    );
    wl_list_insert(surface->frameCallbackList.prev, &callback->link);
}

void WaylandCompositorSubsurface::commitSurface(NestedSurface* surfaceBase, struct wl_client*)
{
    NestedSurfaceSubS* surface = static_cast<NestedSurfaceSubS*>(surfaceBase);
    if (!surface)
        return;

    NestedBufferSubS* buffer = static_cast<NestedBufferSubS*>(surface->buffer);
    // Make the pending buffer current.
    NestedBuffer::reference(&surface->bufferRef, buffer);

    // Destroy any existing EGLImage for this surface.
    if (surface->image != EGL_NO_IMAGE_KHR)
        eglDestroyImage(m_display.eglDisplay, surface->image);

    // Create a new EGLImage from the last buffer attached to this surface
    surface->image = static_cast<EGLImageKHR*>(eglCreateImage(m_display.eglDisplay, EGL_NO_CONTEXT, EGL_WAYLAND_BUFFER_WL, buffer->resource, nullptr));
    if (surface->image == EGL_NO_IMAGE_KHR)
        return;

    buffer->parentBuffer = eglCreateWaylandBufferFromImage(m_display.eglDisplay, surface->image);

    GtkAllocation allocation;
    gtk_widget_get_allocation(surface->widget, &allocation);
    wl_subsurface_set_position(surface->subsurface, allocation.x, allocation.y);

    wl_surface_frame(surface->subsurfaceSurface);
    wl_surface_attach(surface->subsurfaceSurface, buffer->parentBuffer, 0, 0);
    wl_surface_commit(surface->subsurfaceSurface);

    wl_list_remove(&surface->bufferDestroyListener.link);
    surface->buffer = nullptr;

    // Process frame callbacks for this surface so the client can render a new frame.
    NestedFrameCallback* nc;
    NestedFrameCallback* next;
    wl_list_for_each_safe(nc, next, &surface->frameCallbackList, link) {
        wl_callback_send_done(nc->resource, 0);
        wl_resource_destroy(nc->resource);
    }

    wl_list_init(&surface->frameCallbackList);
    wl_display_flush_clients(m_display.childDisplay);

    gtk_widget_queue_draw(surface->widget);
}

void WaylandCompositorSubsurface::render(WaylandCompositor::RenderingContext&)
{
}

bool WaylandCompositorSubsurface::initialize()
{
    if (!WaylandCompositor::initialize())
        return false;

    wl_registry = wl_display_get_registry(m_display.wlDisplay);
    wl_registry_add_listener(wl_registry,&m_registryListener, this);
    wl_display_dispatch(m_display.wlDisplay);

    fprintf(stderr, "WaylandCompositorSubsurface::initialize() -- all OK\n");
    return true;
}

bool WaylandCompositorSubsurface::initializeEGL()
{
#if USE(OPENGL_ES_2)
    static const EGLenum glAPI = EGL_OPENGL_ES_API;
#else
    static const EGLenum glAPI = EGL_OPENGL_API;
#endif

    m_display.eglDisplay = eglGetDisplay(m_display.wlDisplay);
    if (m_display.eglDisplay == EGL_NO_DISPLAY)
        return false;

    if (eglInitialize(m_display.eglDisplay, nullptr, nullptr) == EGL_FALSE)
        return false;

    if (eglBindAPI(glAPI) == EGL_FALSE)
        return false;

    eglCreateImage = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImage = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    if (!eglCreateImage || !eglDestroyImage) {
        g_warning("WaylandCompositorEGL requires eglCreateImageKHR.");
        return false;
    }

    eglQueryBuffer = (PFNEGLQUERYWAYLANDBUFFERWL) eglGetProcAddress("eglQueryWaylandBufferWL");
    if (!eglQueryBuffer) {
        g_warning("WaylandCompositorDispmanX requires eglQueryBuffer.");
        return false;
    }

    eglCreateWaylandBufferFromImage = (PFNEGLCREATEWAYLANDBUFFERFROMIMAGEWL) eglGetProcAddress("eglCreateWaylandBufferFromImageWL");
    if (!eglCreateWaylandBufferFromImage) {
        g_warning("WaylandCompositorSubsurface requires eglCreateWaylandBufferFromImageWL.");
        return false;
    }

    fprintf(stderr, "WaylandCompositorSubsurface::initializeEGL() -- all OK\n");
    return true;
}

NestedSurface* WaylandCompositorSubsurface::createNestedSurface()
{
    return new NestedSurfaceSubS(this);
}

} // namespace WebCore

#endif // USE(EGL) && PLATFORM(WAYLAND)

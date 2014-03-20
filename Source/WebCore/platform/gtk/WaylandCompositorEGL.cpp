#include "config.h"
#include "WaylandCompositorEGL.h"

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <iostream>

#if !defined(PFNEGLQUERYWAYLANDBUFFERWL)
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYWAYLANDBUFFERWL)(EGLDisplay, struct wl_resource*, EGLint, EGLint*);
#endif

#if !defined(EGL_WAYLAND_BUFFER_WL)
#define	EGL_WAYLAND_BUFFER_WL 0x31D5
#endif

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC eglImageTargetTexture2d = nullptr;
static PFNEGLCREATEIMAGEKHRPROC eglCreateImage = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImage = nullptr;
static PFNEGLQUERYWAYLANDBUFFERWL eglQueryBuffer = nullptr;

namespace WebCore {

struct NestedSurfaceEGL : NestedSurface {
    NestedSurfaceEGL(WaylandCompositor* compositor)
        : NestedSurface(compositor)
        , texture(0)
        , image(nullptr) // FIXME: Whay is this a pointer? Why not initialize it to EGL_NO_IMAGE_KHR?
        , cairoSurface(nullptr)
    { }
    virtual ~NestedSurfaceEGL()
    {
        if (image != EGL_NO_IMAGE_KHR)
            eglDestroyImage(compositor->eglDisplay(), image);
        glDeleteTextures(1, &texture);
    }

    GLuint texture;                       // GL texture for the surface
    EGLImageKHR* image;                   // EGL Image for texture
    cairo_surface_t* cairoSurface;        // Cairo surface for GL texture
};

WaylandCompositorEGL::WaylandCompositorEGL()
    : m_eglConfig(nullptr)
    , m_eglContext(EGL_NO_CONTEXT)
    , m_eglDevice(nullptr)
{
}

void WaylandCompositorEGL::attachSurface(NestedSurface* surfaceBase, struct wl_client*, struct wl_resource* bufferResource, int32_t, int32_t)
{
    NestedSurfaceEGL* surface = static_cast<NestedSurfaceEGL*>(surfaceBase);

    EGLint format;
    if (!eglQueryBuffer(m_display->eglDisplay, bufferResource, EGL_TEXTURE_FORMAT, &format))
        return;
    if (format != EGL_TEXTURE_RGB && format != EGL_TEXTURE_RGBA)
        return;

    // Remove references to any previous pending buffer for this surface
    if (surface->buffer) {
        surface->buffer = nullptr;
        wl_list_remove(&surface->bufferDestroyListener.link);
    }

    // Make the new buffer the current pending buffer
    if (bufferResource) {
        surface->buffer = NestedBuffer::fromResource(bufferResource);
        wl_signal_add(&surface->buffer->destroySignal, &surface->bufferDestroyListener);
    }
}

void WaylandCompositorEGL::requestFrame(NestedSurface* surfaceBase, struct wl_client* client, uint32_t id)
{
    NestedSurfaceEGL* surface = static_cast<NestedSurfaceEGL*>(surfaceBase);

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

void WaylandCompositorEGL::commitSurface(NestedSurface* surfaceBase, struct wl_client*)
{
    NestedSurfaceEGL* surface = static_cast<NestedSurfaceEGL*>(surfaceBase);

    // Make the pending buffer current 
    NestedBuffer::reference(&surface->bufferRef, surface->buffer);

    // Destroy any existing EGLImage for this surface
    if (surface->image != EGL_NO_IMAGE_KHR)
        eglDestroyImage(m_display->eglDisplay, surface->image);

    // Destroy any existing cairo surface for this surface
    if (surface->cairoSurface) {
        cairo_surface_destroy(surface->cairoSurface);
        surface->cairoSurface = nullptr;
    }

    // Create a new EGLImage from the last buffer attached to this surface
    surface->image = static_cast<EGLImageKHR*>(eglCreateImage(m_display->eglDisplay, EGL_NO_CONTEXT, EGL_WAYLAND_BUFFER_WL, surface->buffer->resource, nullptr));
    if (surface->image == EGL_NO_IMAGE_KHR)
        return;

    // Bind the surface texture to the new EGLImage
    glBindTexture(GL_TEXTURE_2D, surface->texture);
    eglImageTargetTexture2d(GL_TEXTURE_2D, surface->image);

    // Create a new cairo surface associated with the surface texture
    int width, height;
    eglQueryBuffer(m_display->eglDisplay, surface->buffer->resource, EGL_WIDTH, &width);
    eglQueryBuffer(m_display->eglDisplay, surface->buffer->resource, EGL_HEIGHT, &height);
    surface->cairoSurface = cairo_gl_surface_create_for_texture(m_eglDevice, CAIRO_CONTENT_COLOR_ALPHA, surface->texture, width, height);
    cairo_surface_mark_dirty (surface->cairoSurface); // FIXME: Why do we need this?

    // We are done with this buffer
    if (surface->buffer) {
        wl_list_remove(&surface->bufferDestroyListener.link);
        surface->buffer = nullptr;
    }

    // Redraw the widget backed by this surface
    if (surface->widget)
        gtk_widget_queue_draw(surface->widget);

    // Process frame callbacks for this surface so the client can render a new frame
    NestedFrameCallback *nc, *next;
    wl_list_for_each_safe(nc, next, &surface->frameCallbackList, link) {
        wl_callback_send_done(nc->resource, 0);
        wl_resource_destroy(nc->resource);
    }

    wl_list_init(&surface->frameCallbackList);
    wl_display_flush_clients(m_display->childDisplay);
}

static long long lastFPSDumpTime = 0;
static int frameCount = 0;

void WaylandCompositorEGL::render(WaylandCompositor::RenderingContext& contextBase)
{
    ASSERT(contextBase.type == WaylandCompositor::EGL);
    RenderingContext& context = static_cast<RenderingContext&>(contextBase);

    NestedSurface* surfaceBase = m_widgetHashMap.get(context.widget);
    if (!surfaceBase)
        return;

    NestedSurfaceEGL* surface = static_cast<NestedSurfaceEGL*>(surfaceBase);
    if (!surface->cairoSurface)
        return;

    cairo_rectangle(context.cr, context.clipRect->x, context.clipRect->y, context.clipRect->width, context.clipRect->height);
    cairo_set_source_surface(context.cr, surface->cairoSurface, 0, 0);
    cairo_fill(context.cr);

    frameCount++;
    long long renderTime = g_get_real_time();
    if (renderTime - lastFPSDumpTime >= 5000000) {
        if (lastFPSDumpTime)
            std::cerr << "WaylandCompositorEGL::render(): FPS " << (float)frameCount * 1000000 / (renderTime - lastFPSDumpTime) << std::endl;
        frameCount = 0;
        lastFPSDumpTime = renderTime;
    }
}

bool WaylandCompositorEGL::initializeEGL()
{
    // FIXME: we probably want to get the EGL configuration from GLContextEGL
    static const EGLint contextAttributes[] = {
#if USE(OPENGL_ES_2)
        EGL_CONTEXT_CLIENT_VERSION, 2,
#endif
        EGL_NONE
    };
    static const EGLint configAttributes[] = {
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

#if USE(OPENGL_ES_2)
    static const EGLenum glAPI = EGL_OPENGL_ES_API;
#else
    static const EGLenum glAPI = EGL_OPENGL_API;
#endif

    m_display->eglDisplay = eglGetDisplay(m_display->wlDisplay);
    if (m_display->eglDisplay == EGL_NO_DISPLAY)
        return false;

    if (eglInitialize(m_display->eglDisplay, nullptr, nullptr) == EGL_FALSE)
        return false;

    if (eglBindAPI(glAPI) == EGL_FALSE)
        return false;

    eglCreateImage = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImage = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    if (!eglCreateImage || !eglDestroyImage) {
        g_warning("WaylandCompositorEGL requires eglCreateImageKHR.");
        return false;
    }

    eglImageTargetTexture2d = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!eglImageTargetTexture2d) {
        g_warning("WaylandCompositorEGL requires glEGLImageTargetTexture.");
        return false;
    }

    eglQueryBuffer = (PFNEGLQUERYWAYLANDBUFFERWL) eglGetProcAddress("eglQueryWaylandBufferWL");
    if (!eglQueryBuffer) {
        g_warning("WaylandCompositorEGL requires eglQueryBuffer.");
        return false;
    }

    EGLint n;
    if (eglChooseConfig(m_display->eglDisplay, configAttributes, &m_eglConfig, 1, &n) == EGL_FALSE || n != 1)
        return false;

    m_eglContext = eglCreateContext(m_display->eglDisplay, m_eglConfig, EGL_NO_CONTEXT, contextAttributes);
    if (m_eglContext == EGL_NO_CONTEXT)
        return false;

    if (eglMakeCurrent(m_display->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, m_eglContext) == EGL_FALSE)
        return false;

    m_eglDevice = cairo_egl_device_create(m_display->eglDisplay, m_eglContext);
    if (cairo_device_status(m_eglDevice) != CAIRO_STATUS_SUCCESS)
        return false;

    return true;
}

NestedSurface* WaylandCompositorEGL::createNestedSurface()
{
    NestedSurfaceEGL* surface = new NestedSurfaceEGL(this);

    // Create a GL texture to back this surface
    glGenTextures(1, &surface->texture);
    glBindTexture(GL_TEXTURE_2D, surface->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    return surface;
}

} // namespace WebCore

#endif

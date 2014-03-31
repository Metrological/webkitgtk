#include "config.h"
#include "WaylandCompositorDispmanX.h"

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <fcntl.h>
#include <sys/time.h>

#if !defined(PFNEGLQUERYWAYLANDBUFFERWL)
typedef EGLBoolean (EGLAPIENTRYP PFNEGLQUERYWAYLANDBUFFERWL)(EGLDisplay, struct wl_resource*, EGLint, EGLint*);
#endif

#if !defined(EGL_WAYLAND_BUFFER_WL)
#define	EGL_WAYLAND_BUFFER_WL 0x31D5
#endif

#define ELEMENT_CHANGE_LAYER          (1<<0)
#define ELEMENT_CHANGE_OPACITY        (1<<1)
#define ELEMENT_CHANGE_DEST_RECT      (1<<2)
#define ELEMENT_CHANGE_SRC_RECT       (1<<3)
#define ELEMENT_CHANGE_MASK_RESOURCE  (1<<4)
#define ELEMENT_CHANGE_TRANSFORM      (1<<5)

static PFNEGLQUERYWAYLANDBUFFERWL eglQueryBuffer = nullptr;

namespace WebCore {

struct NestedSurfaceDispmanX : NestedSurface {
    NestedSurfaceDispmanX(WaylandCompositor* compositor)
        : NestedSurface(compositor)
    { }
    virtual ~NestedSurfaceDispmanX() { }
};

// Raspberry Pi flip pipe magic

static uint64_t rpiGetCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void WaylandCompositorDispmanX::rpiFlipPipeUpdateComplete(DISPMANX_UPDATE_HANDLE_T update, void* data)
{
    WaylandCompositorDispmanX* compositor = static_cast<WaylandCompositorDispmanX*>(data);

    uint64_t time = rpiGetCurrentTime();
    ssize_t ret = write(compositor->m_rpiFlipPipe.writefd, &time, sizeof time);
    if (ret != sizeof time)
        g_warning("rpiFlipPipeUpdateComplete: unexpected write outcome");
}

int WaylandCompositorDispmanX::rpiFlipPipeHandler(int fd, uint32_t mask, void* data)
{
    if (mask != WL_EVENT_READABLE)
        g_warning("rpiFlipPipeHandler: unexpected mask\n");

    uint64_t time;
    ssize_t ret = read(fd, &time, sizeof time);
    if (ret != sizeof time)
        g_warning("rpiFlipPipeHandler: unexpected read outcome\n");

    WaylandCompositorDispmanX* compositor = static_cast<WaylandCompositorDispmanX*>(data);
    compositor->queueWidgetRedraw();

    return 1;
}

bool WaylandCompositorDispmanX::initializeRPiFlipPipe()
{
    int fd[2];
    if (pipe2(fd, O_CLOEXEC) == -1)
        return false;

    m_rpiFlipPipe.readfd = fd[0];
    m_rpiFlipPipe.writefd = fd[1];

    struct wl_event_loop* loop = wl_display_get_event_loop(m_display.childDisplay);
    m_rpiFlipPipe.source = wl_event_loop_add_fd(loop, m_rpiFlipPipe.readfd,
        WL_EVENT_READABLE, rpiFlipPipeHandler, this);

    if (!m_rpiFlipPipe.source) {
        close(m_rpiFlipPipe.readfd);
        close(m_rpiFlipPipe.writefd);
        return false;
    }

    return true;
}

WaylandCompositorDispmanX::WaylandCompositorDispmanX()
    : m_dispmanxDisplay(0)
    , m_rpiFlipPipe({ 0, 0, nullptr })
    , m_renderer({ 0, 0, DISPMANX_NO_HANDLE, DISPMANX_NO_HANDLE, DISPMANX_NO_HANDLE })
{
}

void WaylandCompositorDispmanX::attachSurface(NestedSurface* surfaceBase, struct wl_client*, struct wl_resource* bufferResource, int32_t, int32_t)
{
    fprintf(stderr, "WaylandCompositorDispmanX::attachSurface\n");
    NestedSurfaceDispmanX* surface = static_cast<NestedSurfaceDispmanX*>(surfaceBase);

    EGLint format;
    if (!eglQueryBuffer(m_display.eglDisplay, bufferResource, EGL_TEXTURE_FORMAT, &format))
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
    fprintf(stderr, "\tsurface attached\n");
}

void WaylandCompositorDispmanX::requestFrame(NestedSurface* surfaceBase, struct wl_client* client, uint32_t id)
{
    fprintf(stderr, "WaylandCompositorDispmanX::requestFrame\n");
    NestedSurfaceDispmanX* surface = static_cast<NestedSurfaceDispmanX*>(surfaceBase);

    NestedFrameCallback* callback = new NestedFrameCallback(wl_resource_create(client, &wl_callback_interface, 1, id));
    wl_resource_set_implementation(callback->resource, nullptr, callback,
        [](struct wl_resource* resource) {
            NestedFrameCallback* callback = static_cast<NestedFrameCallback*>(wl_resource_get_user_data(resource));
            wl_list_remove(&callback->link);
            delete callback;
        }
    );
    wl_list_insert(surface->frameCallbackList.prev, &callback->link);
    fprintf(stderr, "\tframe requested\n");
}

void WaylandCompositorDispmanX::commitSurface(NestedSurface* surfaceBase, struct wl_client*)
{
    fprintf(stderr, "WaylandCompositorDispmanX::commitSurface %p\n", surfaceBase);
    NestedSurfaceDispmanX* surface = static_cast<NestedSurfaceDispmanX*>(surfaceBase);
    if (!surface)
        return;

    // Make the pending buffer current 
    NestedBuffer::reference(&surface->bufferRef, surface->buffer);

    EGLint width, height;
    EGLDisplay eglDisplay = m_display.eglDisplay;
    if (!eglQueryBuffer(eglDisplay, surface->buffer->resource, EGL_WIDTH, &width)
        || !eglQueryBuffer(eglDisplay, surface->buffer->resource, EGL_HEIGHT, &height))
        return;

    gtk_widget_set_size_request(surface->widget, width, height);

    // Process frame callbacks for this surface so the client can render a new frame
    NestedFrameCallback *nc, *next;
    wl_list_for_each_safe(nc, next, &surface->frameCallbackList, link) {
        wl_callback_send_done(nc->resource, 0);
        wl_resource_destroy(nc->resource);
    }

    wl_list_init(&surface->frameCallbackList);
    wl_display_flush_clients(m_display.childDisplay);
    fprintf(stderr, "\tsurface committed\n");
}

void WaylandCompositorDispmanX::render(WaylandCompositor::RenderingContext& contextBase)
{
    fprintf(stderr, "WaylandCompositorDispmanX::render\n");
    ASSERT(contextBase.type == WaylandCompositor::DispmanX);
    RenderingContext& context = static_cast<RenderingContext&>(contextBase);

    static VC_DISPMANX_ALPHA_T alpha = {
        static_cast<DISPMANX_FLAGS_ALPHA_T>(DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS),
        255, 0
    };

    NestedSurface* targetSurface = nullptr;
    NestedSurface* surface, * nextSurface;
    wl_list_for_each_safe(surface, nextSurface, &m_surfaces, link) {
        // FIXME: The GtkWidget should be guaranteed.
        if (surface->widget == context.widget) {
            targetSurface = surface;
            break;
        }
    }

    if (!targetSurface || !targetSurface->buffer)
        return;

    if (!eglQueryBuffer(m_display.eglDisplay, targetSurface->buffer->resource, EGL_WIDTH, &m_renderer.width)
        || !eglQueryBuffer(m_display.eglDisplay, targetSurface->buffer->resource, EGL_HEIGHT, &m_renderer.height))
        return;

    m_renderer.resource = vc_dispmanx_get_handle_from_wl_buffer(targetSurface->buffer->resource);

    GtkAllocation allocation;
    gtk_widget_get_allocation(targetSurface->widget, &allocation);

    m_renderer.update = vc_dispmanx_update_start(1);
    ASSERT(m_renderer.update);

    VC_RECT_T srcRect, dstRect;
    vc_dispmanx_rect_set(&srcRect, 0 << 16, 0 << 16,
        m_renderer.width << 16, m_renderer.height << 16);
    vc_dispmanx_rect_set(&dstRect, 70, 70, allocation.width, allocation.height);

    if (m_renderer.element == DISPMANX_NO_HANDLE) {
        m_renderer.element = vc_dispmanx_element_add(m_renderer.update,
            m_dispmanxDisplay, 2000, &dstRect, m_renderer.resource, &srcRect,
            DISPMANX_PROTECTION_NONE, &alpha, NULL, DISPMANX_FLIP_VERT);
    } else {
        vc_dispmanx_element_change_source(m_renderer.update, m_renderer.element,
            m_renderer.resource);
        vc_dispmanx_element_modified(m_renderer.update, m_renderer.element, &dstRect);
        int ret = vc_dispmanx_element_change_attributes(m_renderer.update, m_renderer.element,
            ELEMENT_CHANGE_LAYER | ELEMENT_CHANGE_OPACITY | ELEMENT_CHANGE_TRANSFORM
                | ELEMENT_CHANGE_DEST_RECT | ELEMENT_CHANGE_SRC_RECT,
            2000, 255, &dstRect, &srcRect, DISPMANX_NO_HANDLE, DISPMANX_FLIP_VERT);
        ASSERT(ret == 0);
    }

    int ret = vc_dispmanx_update_submit(m_renderer.update, rpiFlipPipeUpdateComplete, this);
    ASSERT(ret == 0);

    wl_list_remove(&targetSurface->bufferDestroyListener.link);
    targetSurface->buffer = NULL;

    m_renderer.update = DISPMANX_NO_HANDLE;
    m_renderer.resource = DISPMANX_NO_HANDLE;

    fprintf(stderr, "\trendered\n");
}

bool WaylandCompositorDispmanX::initialize()
{
    bcm_host_init();

    if (!WaylandCompositor::initialize())
        return false;

    if (!initializeRPiFlipPipe()) {
        g_warning("Could not initalize RPi flip pipe");
        return false;
    }

    m_dispmanxDisplay = vc_dispmanx_display_open(0);
    if (!m_dispmanxDisplay) {
        g_warning("Could not open DispmanX display");
        return false;
    }

    return true;
}

bool WaylandCompositorDispmanX::initializeEGL()
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

    eglQueryBuffer = (PFNEGLQUERYWAYLANDBUFFERWL) eglGetProcAddress("eglQueryWaylandBufferWL");
    if (!eglQueryBuffer) {
        g_warning("WaylandCompositorDispmanX requires eglQueryBuffer.");
        return false;
    }

    return true;
}

NestedSurface* WaylandCompositorDispmanX::createNestedSurface()
{
    return new NestedSurfaceDispmanX(this);
}

void WaylandCompositorDispmanX::queueWidgetRedraw()
{
    fprintf(stderr, "WaylandCompositorDispmanX::queueWidgetRedraw\n");
    NestedSurface *surface, *next;
    wl_list_for_each_safe(surface, next, &m_surfaces, link) {
        // FIXME: The GtkWidget should be guaranteed.
        if (surface->widget)
            gtk_widget_queue_draw(surface->widget);
    }
    fprintf(stderr, "\tqueued\n");
}

} // namespace WebCore

#endif

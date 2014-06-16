#include "config.h"
#include "WaylandCompositorDispmanX.h"

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <fcntl.h>
#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>
#include <sys/time.h>
#include <wayland-client.h>

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
        , subsurfaceSurface(nullptr)
        , subsurface(nullptr)
        , queueRedraw(false)
    { }
    // FIXME: Destroy subsurface.
    virtual ~NestedSurfaceDispmanX() { }

    virtual void setWidget(GtkWidget*) override;
    virtual NestedBuffer* createBuffer(struct wl_resource*) override;

    struct wl_surface* subsurfaceSurface;
    struct wl_subsurface* subsurface;
    bool queueRedraw;
};

struct NestedBufferDispmanX : NestedBuffer {
    NestedBufferDispmanX(struct wl_resource* resource)
        : NestedBuffer(resource)
        , clientBuffer(nullptr)
    {
    }

    struct wl_buffer* clientBuffer;
};

void NestedSurfaceDispmanX::setWidget(GtkWidget* widget)
{
    NestedSurface::setWidget(widget);

    WaylandCompositorDispmanX* dispmanxCompositor = static_cast<WaylandCompositorDispmanX*>(compositor);
    subsurfaceSurface = wl_compositor_create_surface(dispmanxCompositor->wl_compositor);
    subsurface = wl_subcompositor_get_subsurface(dispmanxCompositor->wl_subcompositor, subsurfaceSurface,
        gdk_wayland_window_get_wl_surface(gtk_widget_get_window(widget)));
    fprintf(stderr, "NestedSurfaceDispmanX::setWidget(): acquired subsurface %p, its surface %p\n",
        subsurface, subsurfaceSurface);
    wl_subsurface_set_desync(subsurface);
}

NestedBuffer* NestedSurfaceDispmanX::createBuffer(struct wl_resource* resource)
{
    fprintf(stderr, "NestedSurfaceDispmanX::createBuffer resource %p, handle %d\n",
        resource, vc_dispmanx_get_handle_from_wl_buffer(resource));
    NestedBufferDispmanX* buffer = new NestedBufferDispmanX(resource);

    EGLint width, height;
    if (!eglQueryBuffer(compositor->eglDisplay(), resource, EGL_WIDTH, &width)
        || !eglQueryBuffer(compositor->eglDisplay(), resource, EGL_HEIGHT, &height))
        return nullptr;

    buffer->clientBuffer = wl_dispmanx_create_proxy_buffer(static_cast<WaylandCompositorDispmanX*>(compositor)->wl_dispmanx,
        vc_dispmanx_get_handle_from_wl_buffer(resource), width, height,
        vc_dispmanx_get_format_from_wl_buffer(resource));

    queueRedraw = true;
    return buffer;
}

// Raspberry Pi flip pipe magic

static uint64_t getCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

#if 0
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
#endif

WaylandCompositorDispmanX::WaylandCompositorDispmanX()
    : wl_registry(nullptr)
    , wl_compositor(nullptr)
    , wl_subcompositor(nullptr)
    , wl_dispmanx(nullptr)
{
}

const struct wl_registry_listener WaylandCompositorDispmanX::m_registryListener = {
    // global
    [](void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t)
    {
        WaylandCompositorDispmanX* compositor = static_cast<WaylandCompositorDispmanX*>(data);
        if (strcmp(interface, "wl_compositor") == 0)
            compositor->wl_compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 3));
        else if (strcmp(interface, "wl_subcompositor") == 0)
            compositor->wl_subcompositor = static_cast<struct wl_subcompositor*>(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
        else if (strcmp(interface, "wl_dispmanx") == 0)
            compositor->wl_dispmanx = static_cast<struct wl_dispmanx*>(wl_registry_bind(registry, name, &wl_dispmanx_interface, 1));
    },

    // global_remove
    [](void*, struct wl_registry*, uint32_t)
    {
        // FIXME: if this can happen without the UI Process getting shut down we should probably
        // destroy our cached display instance
    }
};

void WaylandCompositorDispmanX::attachSurface(NestedSurface* surfaceBase, struct wl_client*, struct wl_resource* bufferResource, int32_t, int32_t)
{
    // fprintf(stderr, "WaylandCompositorDispmanX::attachSurface\n");
    NestedSurfaceDispmanX* surface = static_cast<NestedSurfaceDispmanX*>(surfaceBase);

    // Remove references to any previous pending buffer for this surface
    if (surface->buffer) {
        surface->buffer = nullptr;
        wl_list_remove(&surface->bufferDestroyListener.link);
    }

    // Make the new buffer the current pending buffer
    if (bufferResource) {
        surface->buffer = NestedBuffer::fromResource(surface, bufferResource);
        wl_signal_add(&surface->buffer->destroySignal, &surface->bufferDestroyListener);
    }
    //fprintf(stderr, "\tsurface attached\n");
}

void WaylandCompositorDispmanX::requestFrame(NestedSurface* surfaceBase, struct wl_client* client, uint32_t id)
{
    // fprintf(stderr, "WaylandCompositorDispmanX::requestFrame\n");
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
    //fprintf(stderr, "\tframe requested\n");
}

static uint64_t lastCommitPrint = 0;
static int commitCount = 0;

void WaylandCompositorDispmanX::commitSurface(NestedSurface* surfaceBase, struct wl_client*)
{
    // fprintf(stderr, "WaylandCompositorDispmanX::commitSurface %p\n", surfaceBase);
    NestedSurfaceDispmanX* surface = static_cast<NestedSurfaceDispmanX*>(surfaceBase);
    if (!surface)
        return;

    // Make the pending buffer current 
    NestedBuffer::reference(&surface->bufferRef, surface->buffer);

    GtkAllocation allocation;
    gtk_widget_get_allocation(surface->widget, &allocation);
    wl_subsurface_set_position(surface->subsurface, allocation.x, allocation.y);

    wl_surface_frame(surface->subsurfaceSurface);
    wl_surface_attach(surface->subsurfaceSurface,
        static_cast<NestedBufferDispmanX*>(surface->buffer)->clientBuffer, 0, 0);
    wl_surface_commit(surface->subsurfaceSurface);

    wl_list_remove(&surface->bufferDestroyListener.link);
    surface->buffer = NULL;

    // Process frame callbacks for this surface so the client can render a new frame
    NestedFrameCallback *nc, *next;
    wl_list_for_each_safe(nc, next, &surface->frameCallbackList, link) {
        wl_callback_send_done(nc->resource, 0);
        wl_resource_destroy(nc->resource);
    }

    wl_list_init(&surface->frameCallbackList);
    wl_display_flush_clients(m_display.childDisplay);
    //fprintf(stderr, "\tsurface committed\n");

    if (surface->queueRedraw) {
        fprintf(stderr, "\tnew buffer -- queueing redraw\n");
        gtk_widget_queue_draw(surface->widget);
        surface->queueRedraw = false;
    }

    commitCount++;
    uint64_t commitTime = getCurrentTime();
    if (commitTime - lastCommitPrint >= 5000) {
        if (lastCommitPrint == 0) {
            lastCommitPrint = commitTime;
            return;
        }

        g_print ("Committed %d times in the last 5 seconds - CPS %f\n", commitCount, commitCount / 5.0);
        lastCommitPrint = commitTime;
        commitCount = 0;
    }
}

static uint64_t lastRenderPrint = 0;
static int renderCount = 0;

void WaylandCompositorDispmanX::render(WaylandCompositor::RenderingContext& contextBase)
{
    ASSERT(contextBase.type == WaylandCompositor::DispmanX);
    RenderingContext& context = static_cast<RenderingContext&>(contextBase);
    g_print("WaylandCompositorDispmanX::render\n");

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

    GtkAllocation allocation;
    gtk_widget_get_allocation(targetSurface->widget, &allocation);

    NestedSurfaceDispmanX* dispmanxSurface = static_cast<NestedSurfaceDispmanX*>(targetSurface);
    wl_subsurface_set_position(dispmanxSurface->subsurface, allocation.x, allocation.y);

    wl_surface_attach(dispmanxSurface->subsurfaceSurface,
        static_cast<NestedBufferDispmanX*>(targetSurface->buffer)->clientBuffer, 0, 0);
    wl_surface_commit(dispmanxSurface->subsurfaceSurface);

    wl_list_remove(&targetSurface->bufferDestroyListener.link);
    targetSurface->buffer = NULL;

    // Process frame callbacks for this surface so the client can render a new frame
    NestedFrameCallback *nc, *next;
    wl_list_for_each_safe(nc, next, &surface->frameCallbackList, link) {
        wl_callback_send_done(nc->resource, 0);
        wl_resource_destroy(nc->resource);
    }

    wl_list_init(&surface->frameCallbackList);
    wl_display_flush_clients(m_display.childDisplay);
#if 0
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
#endif

    renderCount++;
    uint64_t renderTime = getCurrentTime();
    if (renderTime - lastRenderPrint >= 5000) {
        if (lastRenderPrint == 0) {
            lastRenderPrint = renderTime;
            return;
        }

        g_print ("Rendered %d times in the last 5 seconds - RPS %f\n", renderCount, renderCount / 5.0);
        lastRenderPrint = renderTime;
        renderCount = 0;
    }
}

bool WaylandCompositorDispmanX::initialize()
{
    bcm_host_init();

    if (!WaylandCompositor::initialize())
        return false;

    fprintf(stderr, "WaylandCompositorDispmanX::initialize\n");
    wl_registry = wl_display_get_registry(m_display.wlDisplay);
    fprintf(stderr, "\tdisplay %p, registry %p\n", m_display.wlDisplay, wl_registry);
    wl_registry_add_listener(wl_registry, &m_registryListener, this);
    fprintf(stderr, "\tdispatching\n");
    wl_display_dispatch(m_display.wlDisplay);

    fprintf(stderr, "WaylandCompositorDispmanX::initialize(): compositor %p, subcompositor %p, dispmanx %p\n",
        wl_compositor, wl_subcompositor, wl_dispmanx);

#if 0
    if (!initializeRPiFlipPipe()) {
        g_warning("Could not initalize RPi flip pipe");
        return false;
    }

    m_dispmanxDisplay = vc_dispmanx_display_open(0);
    if (!m_dispmanxDisplay) {
        g_warning("Could not open DispmanX display");
        return false;
    }
#endif

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

#if 0
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
#endif

} // namespace WebCore

#endif

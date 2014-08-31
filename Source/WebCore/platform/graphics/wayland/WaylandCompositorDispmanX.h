#ifndef WaylandCompositorDispmanX_h
#define WaylandCompositorDispmanX_h

#include "WaylandCompositor.h"

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#define BUILD_WAYLAND
#include <bcm_host.h>

namespace WebCore {

class WaylandCompositorDispmanX : public WaylandCompositor {
public:
    WaylandCompositorDispmanX();

    virtual void attachSurface(NestedSurface*, struct wl_client*, struct wl_resource*, int32_t, int32_t) override;
    virtual void requestFrame(NestedSurface*, struct wl_client*, uint32_t) override;
    virtual void commitSurface(NestedSurface*, struct wl_client*) override;

    struct RenderingContext : public WaylandCompositor::RenderingContext {
        RenderingContext(GtkWidget* widget)
            : WaylandCompositor::RenderingContext(WaylandCompositor::DispmanX)
            , widget(widget)
        { }

        GtkWidget* widget;
    };
    virtual void render(WaylandCompositor::RenderingContext&) override;

private:
    virtual bool initialize() override;
    virtual bool initializeEGL() override;
    virtual NestedSurface* createNestedSurface() override;

    bool initializeRPiFlipPipe();
    static void rpiFlipPipeUpdateComplete(DISPMANX_UPDATE_HANDLE_T, void*);
    static int rpiFlipPipeHandler(int, uint32_t, void*);
    void queueWidgetRedraw();

    DISPMANX_DISPLAY_HANDLE_T m_dispmanxDisplay;
    struct {
        int readfd;
        int writefd;
        struct wl_event_source* source;
    } m_rpiFlipPipe;
    struct {
        EGLint width, height;
        DISPMANX_UPDATE_HANDLE_T update;
        DISPMANX_RESOURCE_HANDLE_T resource;
        DISPMANX_ELEMENT_HANDLE_T element;
    } m_renderer;
};

} // namespace WebCore

#endif

#endif // WaylandCompositorDispmanX_h

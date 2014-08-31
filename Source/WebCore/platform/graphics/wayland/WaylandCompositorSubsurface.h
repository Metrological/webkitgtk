#ifndef WaylandCompositorSubsurface_h
#define WaylandCompositorSubsurface_h

#include "WaylandCompositor.h"

#if USE(EGL) && PLATFORM(WAYLAND)

namespace WebCore {

struct NestedBufferSubS;

class WaylandCompositorSubsurface : public WaylandCompositor {
public:
    WaylandCompositorSubsurface();

    virtual void attachSurface(NestedSurface*, struct wl_client*, struct wl_resource*, int32_t, int32_t) override;
    virtual void requestFrame(NestedSurface*, struct wl_client*, uint32_t) override;
    virtual void commitSurface(NestedSurface*, struct wl_client*) override;

    struct RenderingContext : public WaylandCompositor::RenderingContext {
        RenderingContext(GtkWidget* widget)
            : WaylandCompositor::RenderingContext(WaylandCompositor::Subsurface)
            , widget(widget)
        { }

        GtkWidget* widget;
    };
    virtual void render(WaylandCompositor::RenderingContext&) override;

private:
    friend struct NestedSurfaceSubS;

    virtual bool initialize() override;
    virtual bool initializeEGL() override;
    virtual NestedSurface* createNestedSurface() override;

    static const struct wl_registry_listener m_registryListener;

    struct wl_registry* wl_registry;
    struct wl_compositor* wl_compositor;
    struct wl_subcompositor* wl_subcompositor;
};

} // namespace WebCore

#endif // USE(EGL) && PLATFORM(WAYLAND)

#endif // WaylandCompositorSubsurface_h

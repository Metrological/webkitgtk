#ifndef WaylandCompositorEGL_h
#define WaylandCompositorEGL_h

#include "WaylandCompositor.h"

#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cairo.h>
#include <cairo-gl.h>

namespace WebCore {

class WaylandCompositorEGL : public WaylandCompositor {
public:
    WaylandCompositorEGL();

    virtual void attachSurface(NestedSurface*, struct wl_client*, struct wl_resource*, int32_t, int32_t) override;
    virtual void requestFrame(NestedSurface*, struct wl_client*, uint32_t) override;
    virtual void commitSurface(NestedSurface*, struct wl_client*) override;

    struct RenderingContext : public WaylandCompositor::RenderingContext {
        RenderingContext(GtkWidget* widget, cairo_t* cr, GdkRectangle* clipRect)
            : WaylandCompositor::RenderingContext(WaylandCompositor::EGL)
            , widget(widget)
            , cr(cr)
            , clipRect(clipRect)
        { }

        GtkWidget* widget;
        cairo_t* cr;
        GdkRectangle* clipRect;
    };
    virtual void render(WaylandCompositor::RenderingContext&) override;

private:
    virtual bool initializeEGL() override;
    virtual NestedSurface* createNestedSurface() override;

    EGLConfig m_eglConfig;
    EGLContext m_eglContext;
    cairo_device_t* m_eglDevice;
};

} // namespace WebCore

#endif

#endif // WaylandCompositorEGL_h

/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "LayerTreeHostGtk.h"

#if USE(TEXTURE_MAPPER_GL)

#include "DrawingAreaImpl.h"
#include "TextureMapperGL.h"
#include "WebPage.h"
#include "WebProcess.h"

#if USE(OPENGL_ES_2)
#include <GLES2/gl2.h>
#else
#include <GL/gl.h>
#endif

#include <WebCore/FrameView.h>
#include <WebCore/GLContext.h>
#include <WebCore/GraphicsLayerTextureMapper.h>
#include <WebCore/MainFrame.h>
#include <WebCore/Page.h>
#include <WebCore/Settings.h>
#include <wtf/CurrentTime.h>

#include <gdk/gdk.h>
#if PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)
#include <gdk/gdkwayland.h>
#include <WebCore/GLContextEGL.h>
#include <WebCore/WaylandDisplay.h>
#endif

#if PLATFORM(X11) && defined(GDK_WINDOWING_X11)
#define Region XRegion
#define Font XFont
#define Cursor XCursor
#define Screen XScreen
#include <gdk/gdkx.h>
#endif

#if USE(GLX)
#include "GLContextGLX.h"
#endif

using namespace WebCore;

namespace WebKit {

enum {
  DISPLAY_TYPE_NONE = 0,
  DISPLAY_TYPE_X11,
  DISPLAY_TYPE_WAYLAND
};

PassRefPtr<LayerTreeHostGtk> LayerTreeHostGtk::create(WebPage* webPage)
{
    RefPtr<LayerTreeHostGtk> host = adoptRef(new LayerTreeHostGtk(webPage));
    host->initialize();
    return host.release();
}

LayerTreeHostGtk::LayerTreeHostGtk(WebPage* webPage)
    : LayerTreeHost(webPage)
    , m_isValid(true)
    , m_notifyAfterScheduledLayerFlush(false)
    , m_lastFlushTime(0)
    , m_layerFlushSchedulingEnabled(true)
    , m_displayType(getDisplayType())
{
}

int LayerTreeHostGtk::getDisplayType()
{
    GdkDisplay* display = gdk_display_manager_get_default_display(gdk_display_manager_get());
#if PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)
    if (GDK_IS_WAYLAND_DISPLAY(display))
        return DISPLAY_TYPE_WAYLAND;
#endif
#if PLATFORM(X11) && defined(GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY(display))
        return DISPLAY_TYPE_X11;
#endif
    return DISPLAY_TYPE_NONE;
}

GLContext* LayerTreeHostGtk::glContext()
{
    if (m_context)
        return m_context.get();

    if (m_displayType == DISPLAY_TYPE_WAYLAND) {
#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)
        if (!m_wlSurface)
            return 0;
        m_context = m_wlSurface->createGLContext();
        return m_context.get();
#endif
    } else {
        uint64_t windowHandle = m_webPage->nativeWindowHandle();
        if (!windowHandle)
            return 0;
#if !PLATFORM(WAYLAND) // If we are building for both X11 and Wayland we need to force GLX for X11
        m_context = GLContext::createContextForWindow(windowHandle, GLContext::sharingContext());
        return m_context.get();
#elif USE(GLX)
        m_context = GLContextGLX::createContext(windowHandle, GLContext::sharingContext());
        return m_context.get();
#endif
    }
    return 0;
}

void LayerTreeHostGtk::initialize()
{
    m_rootLayer = GraphicsLayer::create(graphicsLayerFactory(), *this);
    m_rootLayer->setDrawsContent(false);
    m_rootLayer->setSize(m_webPage->size());

    // The non-composited contents are a child of the root layer.
    m_nonCompositedContentLayer = GraphicsLayer::create(graphicsLayerFactory(), *this);
    m_nonCompositedContentLayer->setDrawsContent(true);
    m_nonCompositedContentLayer->setContentsOpaque(m_webPage->drawsBackground() && !m_webPage->drawsTransparentBackground());
    m_nonCompositedContentLayer->setSize(m_webPage->size());
    if (m_webPage->corePage()->settings().acceleratedDrawingEnabled())
        m_nonCompositedContentLayer->setAcceleratesDrawing(true);

#ifndef NDEBUG
    m_rootLayer->setName("LayerTreeHost root layer");
    m_nonCompositedContentLayer->setName("LayerTreeHost non-composited content");
#endif

    m_rootLayer->addChild(m_nonCompositedContentLayer.get());
    m_nonCompositedContentLayer->setNeedsDisplay();

    if (m_displayType == DISPLAY_TYPE_WAYLAND) {
#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)
        // Request a wayland surface from the nested wayland compositor
        m_wlSurface = WaylandDisplay::instance()->createSurface(m_webPage->size(), m_webPage->pageID());
        if (!m_wlSurface)
            return;
#endif
    }

    // In the case of Wayland this is not really a native window handle
    // but an internal ID generated by the nested compositor for the widget
    // backed by this surface. We only need it to tell WebKit that we are
    // good to go for AC here
    m_layerTreeContext.contextID = m_webPage->pageID();

    GLContext* context = glContext();
    if (!context)
        return;

    // The creation of the TextureMapper needs an active OpenGL context.
    context->makeContextCurrent();

    m_textureMapper = TextureMapper::create(TextureMapper::OpenGLMode);
    static_cast<TextureMapperGL*>(m_textureMapper.get())->setEnableEdgeDistanceAntialiasing(true);
    toTextureMapperLayer(m_rootLayer.get())->setTextureMapper(m_textureMapper.get());

    // FIXME: Cretae page olverlay layers. https://bugs.webkit.org/show_bug.cgi?id=131433.

    scheduleLayerFlush();
}

LayerTreeHostGtk::~LayerTreeHostGtk()
{
    ASSERT(!m_isValid);
    ASSERT(!m_rootLayer);
    cancelPendingLayerFlush();
}

const LayerTreeContext& LayerTreeHostGtk::layerTreeContext()
{
    return m_layerTreeContext;
}

void LayerTreeHostGtk::setShouldNotifyAfterNextScheduledLayerFlush(bool notifyAfterScheduledLayerFlush)
{
    m_notifyAfterScheduledLayerFlush = notifyAfterScheduledLayerFlush;
}

void LayerTreeHostGtk::setRootCompositingLayer(GraphicsLayer* graphicsLayer)
{
    m_nonCompositedContentLayer->removeAllChildren();

    // Add the accelerated layer tree hierarchy.
    if (graphicsLayer)
        m_nonCompositedContentLayer->addChild(graphicsLayer);

    scheduleLayerFlush();
}

void LayerTreeHostGtk::invalidate()
{
    ASSERT(m_isValid);

    // This can trigger destruction of GL objects so let's make sure that
    // we have the right active context
    if (m_context)
        m_context->makeContextCurrent();

    cancelPendingLayerFlush();
    m_rootLayer = nullptr;
    m_nonCompositedContentLayer = nullptr;
    m_textureMapper = nullptr;

    m_context = nullptr;
    m_isValid = false;
}

void LayerTreeHostGtk::setNonCompositedContentsNeedDisplay()
{
    m_nonCompositedContentLayer->setNeedsDisplay();

    PageOverlayLayerMap::iterator end = m_pageOverlayLayers.end();
    for (PageOverlayLayerMap::iterator it = m_pageOverlayLayers.begin(); it != end; ++it)
        it->value->setNeedsDisplay();

    scheduleLayerFlush();
}

void LayerTreeHostGtk::setNonCompositedContentsNeedDisplayInRect(const IntRect& rect)
{
    m_nonCompositedContentLayer->setNeedsDisplayInRect(rect);

    PageOverlayLayerMap::iterator end = m_pageOverlayLayers.end();
    for (PageOverlayLayerMap::iterator it = m_pageOverlayLayers.begin(); it != end; ++it)
        it->value->setNeedsDisplayInRect(rect);

    scheduleLayerFlush();
}

void LayerTreeHostGtk::scrollNonCompositedContents(const IntRect& scrollRect)
{
    setNonCompositedContentsNeedDisplayInRect(scrollRect);
}

void LayerTreeHostGtk::sizeDidChange(const IntSize& newSize)
{
    if (m_rootLayer->size() == newSize)
        return;
    m_rootLayer->setSize(newSize);

    if (m_displayType == DISPLAY_TYPE_WAYLAND) {
#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)
        m_wlSurface->resize(newSize);
#endif
    }

    // If the newSize exposes new areas of the non-composited content a setNeedsDisplay is needed
    // for those newly exposed areas.
    FloatSize oldSize = m_nonCompositedContentLayer->size();
    m_nonCompositedContentLayer->setSize(newSize);

    if (newSize.width() > oldSize.width()) {
        float height = std::min(static_cast<float>(newSize.height()), oldSize.height());
        m_nonCompositedContentLayer->setNeedsDisplayInRect(FloatRect(oldSize.width(), 0, newSize.width() - oldSize.width(), height));
    }

    if (newSize.height() > oldSize.height())
        m_nonCompositedContentLayer->setNeedsDisplayInRect(FloatRect(0, oldSize.height(), newSize.width(), newSize.height() - oldSize.height()));
    m_nonCompositedContentLayer->setNeedsDisplay();

    PageOverlayLayerMap::iterator end = m_pageOverlayLayers.end();
    for (PageOverlayLayerMap::iterator it = m_pageOverlayLayers.begin(); it != end; ++it)
        it->value->setSize(newSize);

    compositeLayersToContext(ForResize);
}

void LayerTreeHostGtk::deviceOrPageScaleFactorChanged()
{
    // Other layers learn of the scale factor change via WebPage::setDeviceScaleFactor.
    m_nonCompositedContentLayer->deviceOrPageScaleFactorChanged();
}

void LayerTreeHostGtk::forceRepaint()
{
    scheduleLayerFlush();
}

void LayerTreeHostGtk::didInstallPageOverlay(PageOverlay* pageOverlay)
{
    createPageOverlayLayer(pageOverlay);
    scheduleLayerFlush();
}

void LayerTreeHostGtk::didUninstallPageOverlay(PageOverlay* pageOverlay)
{
    destroyPageOverlayLayer(pageOverlay);
    scheduleLayerFlush();
}

void LayerTreeHostGtk::setPageOverlayNeedsDisplay(PageOverlay* pageOverlay, const IntRect& rect)
{
    GraphicsLayer* layer = m_pageOverlayLayers.get(pageOverlay);
    if (!layer)
        return;

    layer->setNeedsDisplayInRect(rect);
    scheduleLayerFlush();
}

void LayerTreeHostGtk::paintContents(const GraphicsLayer* graphicsLayer, GraphicsContext& graphicsContext, GraphicsLayerPaintingPhase, const FloatRect& clipRect)
{
    if (graphicsLayer == m_nonCompositedContentLayer.get()) {
        m_webPage->drawRect(graphicsContext, enclosingIntRect(clipRect));
        return;
    }

    // FIXME: Draw page overlays. https://bugs.webkit.org/show_bug.cgi?id=131433.
}

void LayerTreeHostGtk::layerFlushTimerFired()
{
    flushAndRenderLayers();

    if (!m_layerFlushTimerCallback.isScheduled() && toTextureMapperLayer(m_rootLayer.get())->descendantsOrSelfHaveRunningAnimations()) {
        const double targetFPS = 60;
        double nextFlush = std::max((1 / targetFPS) - (currentTime() - m_lastFlushTime), 0.0);
        m_layerFlushTimerCallback.scheduleAfterDelay("[WebKit] layerFlushTimer", std::bind(&LayerTreeHostGtk::layerFlushTimerFired, this),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(nextFlush)), GDK_PRIORITY_EVENTS);
    }
}

bool LayerTreeHostGtk::flushPendingLayerChanges()
{
    m_rootLayer->flushCompositingStateForThisLayerOnly();
    m_nonCompositedContentLayer->flushCompositingStateForThisLayerOnly();

    PageOverlayLayerMap::iterator end = m_pageOverlayLayers.end();
    for (PageOverlayLayerMap::iterator it = m_pageOverlayLayers.begin(); it != end; ++it)
        it->value->flushCompositingStateForThisLayerOnly();

    return m_webPage->corePage()->mainFrame().view()->flushCompositingStateIncludingSubframes();
}

void LayerTreeHostGtk::compositeLayersToContext(CompositePurpose purpose)
{
    GLContext* context = glContext();
    if (!context || !context->makeContextCurrent())
        return;

    // The window size may be out of sync with the page size at this point, and getting
    // the viewport parameters incorrect, means that the content will be misplaced. Thus
    // we set the viewport parameters directly from the window size.
    IntSize contextSize = m_context->defaultFrameBufferSize();
    glViewport(0, 0, contextSize.width(), contextSize.height());

    if (purpose == ForResize) {
        glClearColor(1, 1, 1, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    m_textureMapper->beginPainting();
    toTextureMapperLayer(m_rootLayer.get())->paint();
    m_textureMapper->endPainting();

    context->swapBuffers();
}

void LayerTreeHostGtk::flushAndRenderLayers()
{
    {
        RefPtr<LayerTreeHostGtk> protect(this);
        m_webPage->layoutIfNeeded();

        if (!m_isValid)
            return;
    }

    GLContext* context = glContext();
    if (!context || !context->makeContextCurrent())
        return;


#if USE(EGL) && PLATFORM(WAYLAND) && defined(GDK_WINDOWING_WAYLAND)
    if (m_displayType == DISPLAY_TYPE_WAYLAND && m_wlSurface)
        m_wlSurface->requestFrame();
#endif

    m_lastFlushTime = currentTime();
    if (!flushPendingLayerChanges())
        return;

    // Our model is very simple. We always composite and render the tree immediately after updating it.
    compositeLayersToContext();

    if (m_notifyAfterScheduledLayerFlush) {
        // Let the drawing area know that we've done a flush of the layer changes.
        static_cast<DrawingAreaImpl*>(m_webPage->drawingArea())->layerHostDidFlushLayers();
        m_notifyAfterScheduledLayerFlush = false;
    }
}

void LayerTreeHostGtk::createPageOverlayLayer(PageOverlay* pageOverlay)
{
    std::unique_ptr<GraphicsLayer> layer = GraphicsLayer::create(graphicsLayerFactory(), *this);
#ifndef NDEBUG
    layer->setName("LayerTreeHost page overlay content");
#endif

    layer->setAcceleratesDrawing(m_webPage->corePage()->settings().acceleratedDrawingEnabled());
    layer->setDrawsContent(true);
    layer->setSize(m_webPage->size());
    layer->setShowDebugBorder(m_webPage->corePage()->settings().showDebugBorders());
    layer->setShowRepaintCounter(m_webPage->corePage()->settings().showRepaintCounter());

    m_rootLayer->addChild(layer.get());
    m_pageOverlayLayers.add(pageOverlay, WTF::move(layer));
}

void LayerTreeHostGtk::destroyPageOverlayLayer(PageOverlay* pageOverlay)
{
    std::unique_ptr<GraphicsLayer> layer = m_pageOverlayLayers.take(pageOverlay);
    ASSERT(layer);

    layer->removeFromParent();
}

void LayerTreeHostGtk::scheduleLayerFlush()
{
    if (!m_layerFlushSchedulingEnabled)
        return;

    // We use a GLib timer because otherwise GTK+ event handling during dragging can starve WebCore timers, which have a lower priority.
    if (!m_layerFlushTimerCallback.isScheduled())
        m_layerFlushTimerCallback.schedule("[WebKit] layerFlushTimer", std::bind(&LayerTreeHostGtk::layerFlushTimerFired, this), GDK_PRIORITY_EVENTS);
}

void LayerTreeHostGtk::setLayerFlushSchedulingEnabled(bool layerFlushingEnabled)
{
    if (m_layerFlushSchedulingEnabled == layerFlushingEnabled)
        return;

    m_layerFlushSchedulingEnabled = layerFlushingEnabled;

    if (m_layerFlushSchedulingEnabled) {
        scheduleLayerFlush();
        return;
    }

    cancelPendingLayerFlush();
}

void LayerTreeHostGtk::pageBackgroundTransparencyChanged()
{
    m_nonCompositedContentLayer->setContentsOpaque(m_webPage->drawsBackground() && !m_webPage->drawsTransparentBackground());
}

void LayerTreeHostGtk::cancelPendingLayerFlush()
{
    m_layerFlushTimerCallback.cancel();
}

} // namespace WebKit

#endif

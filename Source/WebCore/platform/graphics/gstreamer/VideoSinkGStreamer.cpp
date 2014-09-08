/*
 *  Copyright (C) 2007 OpenedHand
 *  Copyright (C) 2007 Alp Toker <alp@atoker.com>
 *  Copyright (C) 2009, 2010, 2011, 2012 Igalia S.L
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 *
 * WebKitVideoSink is a GStreamer sink element that triggers
 * repaints in the WebKit GStreamer media player for the
 * current video buffer.
 */

#include "config.h"
#include "VideoSinkGStreamer.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)
#include "GRefPtrGStreamer.h"
#include "GStreamerUtilities.h"
#include "IntSize.h"
#include <glib.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <wtf/Threading.h>
#include <wtf/OwnPtr.h>
#include <wtf/gobject/GMainLoopSource.h>
#include <wtf/gobject/GMutexLocker.h>

#if USE(EGL)
#define WL_EGL_PLATFORM
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#if USE(OPENGL_ES_2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#if GST_CHECK_VERSION(1, 3, 0)
#include <gst/gl/egl/gsteglimagememory.h>
#include <gst/gl/gstglutils.h>

#endif
#endif

using namespace WebCore;

// CAIRO_FORMAT_RGB24 used to render the video buffers is little/big endian dependant.
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 3, 0)
#define GST_CAPS_FORMAT "{ RGBA }"
#else
#define GST_CAPS_FORMAT "{ BGRx, BGRA }"
#endif
#else
#define GST_CAPS_FORMAT "{ xRGB, ARGB }"
#endif

#if GST_CHECK_VERSION(1, 1, 0)
#define GST_FEATURED_CAPS_GL GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, GST_CAPS_FORMAT) ";"
#if GST_CHECK_VERSION(1, 3, 0)
#define GST_FEATURED_CAPS GST_FEATURED_CAPS_GL GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_EGL_IMAGE, GST_CAPS_FORMAT) ";"
#else
#define GST_FEATURED_CAPS GST_FEATURED_CAPS_GL
#endif
#else
#define GST_FEATURED_CAPS
#endif

#define WEBKIT_VIDEO_SINK_PAD_CAPS GST_FEATURED_CAPS GST_VIDEO_CAPS_MAKE(GST_CAPS_FORMAT)

static GstStaticPadTemplate s_sinkTemplate = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(WEBKIT_VIDEO_SINK_PAD_CAPS));


GST_DEBUG_CATEGORY_STATIC(webkitVideoSinkDebug);
#define GST_CAT_DEFAULT webkitVideoSinkDebug

enum {
    REPAINT_REQUESTED,
    DRAIN,
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_CAPS
};

static guint webkitVideoSinkSignals[LAST_SIGNAL] = { 0, };

struct _WebKitVideoSinkPrivate {
    _WebKitVideoSinkPrivate()
    {
        g_mutex_init(&bufferMutex);
        g_cond_init(&dataCondition);
        gst_video_info_init(&info);

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 3, 0)
        g_object_set(GST_BASE_SINK(sink), "enable-last-sample", FALSE, NULL);
        pool = NULL;
        last_buffer = NULL;

        allocateCondition = g_new0(GCond, 1);
        g_cond_init(allocateCondition);
        allocateMutex = g_new0(GMutex, 1);
        g_mutex_init(allocateMutex);
        allocateBuffer = NULL;
#endif
    }

    ~_WebKitVideoSinkPrivate()
    {
        g_mutex_clear(&bufferMutex);
        g_cond_clear(&dataCondition);

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 3, 0)
        if (sink->priv->allocateCondition) {
            g_cond_clear(priv->allocateCondition);
            g_free(priv->allocateCondition);
        }

        if (sink->priv->allocateMutex) {
            g_mutex_clear(priv->allocateMutex);
            g_free(priv->allocateMutex);
        }
#endif
    }

    GstBuffer* buffer;
    GMainLoopSource timeoutSource;
    GMutex bufferMutex;
    GCond dataCondition;

    GstVideoInfo info;

    GstCaps* currentCaps;

    // If this is TRUE all processing should finish ASAP
    // This is necessary because there could be a race between
    // unlock() and render(), where unlock() wins, signals the
    // GCond, then render() tries to render a frame although
    // everything else isn't running anymore. This will lead
    // to deadlocks because render() holds the stream lock.
    //
    // Protected by the buffer mutex
    bool unlocked;

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 3, 0)
    GstBufferPool *pool;
    GstBuffer *last_buffer;

    GstGLDisplay *display;
    GstGLContext *context;
    GstGLContext *other_context;

    GCond* allocateCondition;
    GMutex* allocateMutex;
    GstBuffer* allocateBuffer;
#endif
};

#define webkit_video_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(WebKitVideoSink, webkit_video_sink, GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT(webkitVideoSinkDebug, "webkitsink", 0, "webkit video sink"));

static gboolean _ensure_gl_setup(WebKitVideoSink* gl_sink)
{
    GError* error = NULL;

    if (!gst_gl_ensure_display(gl_sink, &gl_sink->priv->display))
        return FALSE;

    if (!gl_sink->priv->context) {
        gl_sink->priv->context = gst_gl_context_new(gl_sink->priv->display);

        if (!gst_gl_context_create(gl_sink->priv->context, gl_sink->priv->other_context, &error)) {
            GST_ELEMENT_ERROR(gl_sink, RESOURCE, NOT_FOUND, ("%s", error->message), (NULL));
            gst_object_unref(gl_sink->priv->context);
            gl_sink->priv->context = NULL;
            return FALSE;
        }
    }

    return TRUE;
}

static void webkit_video_sink_init(WebKitVideoSink* sink)
{
    sink->priv = G_TYPE_INSTANCE_GET_PRIVATE(sink, WEBKIT_TYPE_VIDEO_SINK, WebKitVideoSinkPrivate);
    new (sink->priv) WebKitVideoSinkPrivate();
}

static void webkitVideoSinkTimeoutCallback(WebKitVideoSink* sink)
{
    WebKitVideoSinkPrivate* priv = sink->priv;

    GMutexLocker lock(priv->bufferMutex);
    GstBuffer* buffer = priv->buffer;
    priv->buffer = 0;

    if (!buffer || priv->unlocked || UNLIKELY(!GST_IS_BUFFER(buffer))) {
        g_cond_signal(&priv->dataCondition);
        return;
    }

    g_signal_emit(sink, webkitVideoSinkSignals[REPAINT_REQUESTED], 0, buffer);
#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 3, 0)
    gst_buffer_replace (&priv->last_buffer, buffer);
#endif
    gst_buffer_unref(buffer);
    g_cond_signal(&priv->dataCondition);
}

static GstFlowReturn webkitVideoSinkRender(GstBaseSink* baseSink, GstBuffer* buffer)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    GMutexLocker lock(priv->bufferMutex);

    if (priv->unlocked)
        return GST_FLOW_OK;

    priv->buffer = gst_buffer_ref(buffer);

    // The video info structure is valid only if the sink handled an allocation query.
    GstVideoFormat format = GST_VIDEO_INFO_FORMAT(&priv->info);
    if (format == GST_VIDEO_FORMAT_UNKNOWN) {
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }

#if !(USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS))
    // Cairo's ARGB has pre-multiplied alpha while GStreamer's doesn't.
    // Here we convert to Cairo's ARGB.
    if (format == GST_VIDEO_FORMAT_ARGB || format == GST_VIDEO_FORMAT_BGRA) {
        // Because GstBaseSink::render() only owns the buffer reference in the
        // method scope we can't use gst_buffer_make_writable() here. Also
        // The buffer content should not be changed here because the same buffer
        // could be passed multiple times to this method (in theory).

        GstBuffer* newBuffer = WebCore::createGstBuffer(buffer);

        // Check if allocation failed.
        if (UNLIKELY(!newBuffer)) {
            gst_buffer_unref(buffer);
            return GST_FLOW_ERROR;
        }

        // We don't use Color::premultipliedARGBFromColor() here because
        // one function call per video pixel is just too expensive:
        // For 720p/PAL for example this means 1280*720*25=23040000
        // function calls per second!
        GstVideoFrame sourceFrame;
        GstVideoFrame destinationFrame;

        if (!gst_video_frame_map(&sourceFrame, &priv->info, buffer, GST_MAP_READ)) {
            gst_buffer_unref(buffer);
            gst_buffer_unref(newBuffer);
            return GST_FLOW_ERROR;
        }
        if (!gst_video_frame_map(&destinationFrame, &priv->info, newBuffer, GST_MAP_WRITE)) {
            gst_video_frame_unmap(&sourceFrame);
            gst_buffer_unref(buffer);
            gst_buffer_unref(newBuffer);
            return GST_FLOW_ERROR;
        }

        const guint8* source = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&sourceFrame, 0));
        guint8* destination = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&destinationFrame, 0));

        for (int x = 0; x < GST_VIDEO_FRAME_HEIGHT(&sourceFrame); x++) {
            for (int y = 0; y < GST_VIDEO_FRAME_WIDTH(&sourceFrame); y++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                unsigned short alpha = source[3];
                destination[0] = (source[0] * alpha + 128) / 255;
                destination[1] = (source[1] * alpha + 128) / 255;
                destination[2] = (source[2] * alpha + 128) / 255;
                destination[3] = alpha;
#else
                unsigned short alpha = source[0];
                destination[0] = alpha;
                destination[1] = (source[1] * alpha + 128) / 255;
                destination[2] = (source[2] * alpha + 128) / 255;
                destination[3] = (source[3] * alpha + 128) / 255;
#endif
                source += 4;
                destination += 4;
            }
        }

        gst_video_frame_unmap(&sourceFrame);
        gst_video_frame_unmap(&destinationFrame);
        gst_buffer_unref(buffer);
        buffer = priv->buffer = newBuffer;
    }
#endif

    // This should likely use a lower priority, but glib currently starves
    // lower priority sources.
    // See: https://bugzilla.gnome.org/show_bug.cgi?id=610830.
    gst_object_ref(sink);
    priv->timeoutSource.schedule("[WebKit] webkitVideoSinkTimeoutCallback", std::function<void()>(std::bind(webkitVideoSinkTimeoutCallback, sink)), G_PRIORITY_DEFAULT,
        [sink] { gst_object_unref(sink); });

    g_cond_wait(&priv->dataCondition, &priv->bufferMutex);
    return GST_FLOW_OK;
}

static void webkitVideoSinkFinalize(GObject* object)
{
    WEBKIT_VIDEO_SINK(object)->priv->~WebKitVideoSinkPrivate();
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void webkitVideoSinkGetProperty(GObject* object, guint propertyId, GValue* value, GParamSpec* parameterSpec)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(object);
    WebKitVideoSinkPrivate* priv = sink->priv;

    switch (propertyId) {
    case PROP_CAPS: {
        GstCaps* caps = priv->currentCaps;
        if (caps)
            gst_caps_ref(caps);
        g_value_take_boxed(value, caps);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propertyId, parameterSpec);
    }
}

static void unlockBufferMutex(WebKitVideoSinkPrivate* priv)
{
    GMutexLocker lock(priv->bufferMutex);

    if (priv->buffer) {
        gst_buffer_unref(priv->buffer);
        priv->buffer = 0;
    }

    priv->unlocked = true;

    g_cond_signal(&priv->dataCondition);
}

static gboolean webkitVideoSinkUnlock(GstBaseSink* baseSink)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);

    unlockBufferMutex(sink->priv);

    return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, unlock, (baseSink), TRUE);
}

static gboolean webkitVideoSinkUnlockStop(GstBaseSink* baseSink)
{
    WebKitVideoSinkPrivate* priv = WEBKIT_VIDEO_SINK(baseSink)->priv;

    {
        GMutexLocker lock(priv->bufferMutex);
        priv->unlocked = false;
    }

    return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, unlock_stop, (baseSink), TRUE);
}

static gboolean webkitVideoSinkStop(GstBaseSink* baseSink)
{
    WebKitVideoSink* sink = reinterpret_cast_ptr<WebKitVideoSink*>(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    unlockBufferMutex(priv);

    if (priv->currentCaps) {
        gst_caps_unref(priv->currentCaps);
        priv->currentCaps = 0;
    }

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 3, 0)
    GST_OBJECT_LOCK (sink);
    if (priv->last_buffer)
        gst_buffer_replace (&priv->last_buffer, NULL);
    if (priv->pool)
        gst_object_unref (priv->pool);
    priv->pool = NULL;
    GST_OBJECT_UNLOCK (sink);
#endif

    return TRUE;
}

static gboolean webkitVideoSinkStart(GstBaseSink* baseSink)
{
    WebKitVideoSinkPrivate* priv = WEBKIT_VIDEO_SINK(baseSink)->priv;

    GMutexLocker lock(priv->bufferMutex);
    priv->unlocked = false;
    return TRUE;
}

static gboolean webkitVideoSinkSetCaps(GstBaseSink* baseSink, GstCaps* caps)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    GST_DEBUG_OBJECT(sink, "Current caps %" GST_PTR_FORMAT ", setting caps %" GST_PTR_FORMAT, priv->currentCaps, caps);

    GstVideoInfo videoInfo;
    gst_video_info_init(&videoInfo);
    if (!gst_video_info_from_caps(&videoInfo, caps)) {
        GST_ERROR_OBJECT(sink, "Invalid caps %" GST_PTR_FORMAT, caps);
        return FALSE;
    }

    priv->info = videoInfo;
    gst_caps_replace(&priv->currentCaps, caps);
    return TRUE;
}

static gboolean webkitVideoSinkProposeAllocation(GstBaseSink* baseSink, GstQuery* query)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    GstCaps* caps = NULL;
    gboolean need_pool;

    gst_query_parse_allocation(query, &caps, &need_pool);
    if (!caps)
        return FALSE;

    if (!gst_video_info_from_caps(&sink->priv->info, caps))
        return FALSE;

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 3, 0)
    // Code adapted from gst-plugins-bad's glimagesink.

    GstBufferPool* pool;
    GstStructure* config;
    guint size;
    GstAllocator* allocator = 0;
    GstAllocationParams params;

    if (!_ensure_gl_setup(sink))
        return FALSE;

    if ((pool = sink->priv->pool))
        gst_object_ref(pool);

    if (pool) {
        GstCaps* pcaps;

        // We had a pool, check its caps.
        GST_DEBUG_OBJECT (sink, "check existing pool caps");
        config = gst_buffer_pool_get_config(pool);
        gst_buffer_pool_config_get_params(config, &pcaps, &size, 0, 0);

        if (!gst_caps_is_equal(caps, pcaps)) {
            GST_DEBUG_OBJECT(sink, "pool has different caps");
            // Different caps, we can't use this pool.
            gst_object_unref(pool);
            pool = 0;
        }
        gst_structure_free(config);
    }

    if (need_pool && !pool) {
        GstVideoInfo info;

        if (!gst_video_info_from_caps(&info, caps)) {
            GST_DEBUG_OBJECT(sink, "invalid caps specified");
            return FALSE;
        }

        GST_DEBUG_OBJECT(sink, "create new pool");
        pool = gst_gl_buffer_pool_new(sink->priv->context);

        // The normal size of a frame.
        size = info.size;

        config = gst_buffer_pool_get_config(pool);
        gst_buffer_pool_config_set_params(config, caps, size, 0, 0);
        if (!gst_buffer_pool_set_config(pool, config)) {
            GST_DEBUG_OBJECT(sink, "failed setting config");
            return FALSE;
        }
    }

    // We need at least 2 buffer because we hold on to the last one.
    if (pool) {
        gst_query_add_allocation_pool(query, pool, size, 2, 0);
        gst_object_unref(pool);
    }

    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, 0);

    gst_allocation_params_init(&params);
    allocator = gst_allocator_find(GST_EGL_IMAGE_MEMORY_TYPE);
    gst_query_add_allocation_param(query, allocator, &params);
    gst_object_unref(allocator);
#else
    gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, 0);
    gst_query_add_allocation_meta(query, GST_VIDEO_CROP_META_API_TYPE, 0);
    gst_query_add_allocation_meta(query, GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, 0);
#endif
    return TRUE;
}

static gboolean webkitVideoSinkQuery(GstBaseSink* baseSink, GstQuery* query)
{
    WebKitVideoSink* sink = WEBKIT_VIDEO_SINK(baseSink);
    WebKitVideoSinkPrivate* priv = sink->priv;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_DRAIN:
    {
#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 3, 0)
        GST_OBJECT_LOCK (sink);
        if (priv->last_buffer)
            gst_buffer_replace (&priv->last_buffer, NULL);
        g_signal_emit(sink, webkitVideoSinkSignals[DRAIN], 0);
        GST_OBJECT_UNLOCK (sink);
#endif
        return TRUE;
    }
    case GST_QUERY_CONTEXT:
    {
        return gst_gl_handle_context_query(GST_ELEMENT(sink), query, &priv->display);
      break;
    }
    default:
        return GST_CALL_PARENT_WITH_DEFAULT(GST_BASE_SINK_CLASS, query, (baseSink, query), TRUE);
      break;
    }
}

#if GST_CHECK_VERSION(1, 3, 0)
static void
webkitVideoSinkSetContext(GstElement* element, GstContext* context)
{
    WebKitVideoSink* sink =  WEBKIT_VIDEO_SINK(element);

    gst_gl_handle_set_context(element, context, &sink->priv->display);
}
#endif

static void webkit_video_sink_class_init(WebKitVideoSinkClass* klass)
{
    GObjectClass* gobjectClass = G_OBJECT_CLASS(klass);
    GstBaseSinkClass* baseSinkClass = GST_BASE_SINK_CLASS(klass);
    GstElementClass* elementClass = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_pad_template(elementClass, gst_static_pad_template_get(&s_sinkTemplate));
    gst_element_class_set_metadata(elementClass, "WebKit video sink", "Sink/Video", "Sends video data from a GStreamer pipeline to WebKit", "Igalia, Alp Toker <alp@atoker.com>");

    g_type_class_add_private(klass, sizeof(WebKitVideoSinkPrivate));

    gobjectClass->finalize = webkitVideoSinkFinalize;
    gobjectClass->get_property = webkitVideoSinkGetProperty;

    baseSinkClass->unlock = webkitVideoSinkUnlock;
    baseSinkClass->unlock_stop = webkitVideoSinkUnlockStop;
    baseSinkClass->render = webkitVideoSinkRender;
    baseSinkClass->preroll = webkitVideoSinkRender;
    baseSinkClass->stop = webkitVideoSinkStop;
    baseSinkClass->start = webkitVideoSinkStart;
    baseSinkClass->set_caps = webkitVideoSinkSetCaps;
    baseSinkClass->propose_allocation = webkitVideoSinkProposeAllocation;
    baseSinkClass->query = webkitVideoSinkQuery;

#if GST_CHECK_VERSION(1, 3, 0)
    elementClass->set_context = webkitVideoSinkSetContext;
#endif

    g_object_class_install_property(gobjectClass, PROP_CAPS,
        g_param_spec_boxed("current-caps", "Current-Caps", "Current caps", GST_TYPE_CAPS, G_PARAM_READABLE));

    webkitVideoSinkSignals[REPAINT_REQUESTED] = g_signal_new("repaint-requested",
            G_TYPE_FROM_CLASS(klass),
            static_cast<GSignalFlags>(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
            0, // Class offset
            0, // Accumulator
            0, // Accumulator data
            g_cclosure_marshal_generic,
            G_TYPE_NONE, // Return type
            1, // Only one parameter
            GST_TYPE_BUFFER);

    webkitVideoSinkSignals[DRAIN] = g_signal_new("drain",
            G_TYPE_FROM_CLASS(klass),
            static_cast<GSignalFlags>(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
            0, // Class offset
            0, // Accumulator
            0, // Accumulator data
            g_cclosure_marshal_generic,
            G_TYPE_NONE, // Return type
            0 // No parameters
            );
}


GstElement* webkitVideoSinkNew()
{
    return GST_ELEMENT(g_object_new(WEBKIT_TYPE_VIDEO_SINK, 0));
}

#endif // ENABLE(VIDEO) && USE(GSTREAMER)

/* 
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2010-2011 Intel Corporation
 * Copyright © 2013 Raspberry Pi Foundation
 * 
 * Permission to use, copy, modify, distribute, and sell this
 * software and its documentation for any purpose is hereby granted
 * without fee, provided that\n the above copyright notice appear in
 * all copies and that both that copyright notice and this permission
 * notice appear in supporting documentation, and that the name of
 * the copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
 * THIS SOFTWARE.
 */

#ifndef DISPMANX_SERVER_PROTOCOL_H
#define DISPMANX_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-util.h"

struct wl_client;
struct wl_resource;

struct wl_dispmanx;

extern const struct wl_interface wl_dispmanx_interface;

#ifndef WL_DISPMANX_ERROR_ENUM
#define WL_DISPMANX_ERROR_ENUM
enum wl_dispmanx_error {
	WL_DISPMANX_ERROR_ALLOC_FAILED = 0,
	WL_DISPMANX_ERROR_INVALID_FORMAT = 1,
};
#endif /* WL_DISPMANX_ERROR_ENUM */

#ifndef WL_DISPMANX_FORMAT_ENUM
#define WL_DISPMANX_FORMAT_ENUM
enum wl_dispmanx_format {
	WL_DISPMANX_FORMAT_C8 = 0x20203843,
	WL_DISPMANX_FORMAT_RGB332 = 0x38424752,
	WL_DISPMANX_FORMAT_BGR233 = 0x38524742,
	WL_DISPMANX_FORMAT_XRGB4444 = 0x32315258,
	WL_DISPMANX_FORMAT_XBGR4444 = 0x32314258,
	WL_DISPMANX_FORMAT_RGBX4444 = 0x32315852,
	WL_DISPMANX_FORMAT_BGRX4444 = 0x32315842,
	WL_DISPMANX_FORMAT_ARGB4444 = 0x32315241,
	WL_DISPMANX_FORMAT_ABGR4444 = 0x32314241,
	WL_DISPMANX_FORMAT_RGBA4444 = 0x32314152,
	WL_DISPMANX_FORMAT_BGRA4444 = 0x32314142,
	WL_DISPMANX_FORMAT_XRGB1555 = 0x35315258,
	WL_DISPMANX_FORMAT_XBGR1555 = 0x35314258,
	WL_DISPMANX_FORMAT_RGBX5551 = 0x35315852,
	WL_DISPMANX_FORMAT_BGRX5551 = 0x35315842,
	WL_DISPMANX_FORMAT_ARGB1555 = 0x35315241,
	WL_DISPMANX_FORMAT_ABGR1555 = 0x35314241,
	WL_DISPMANX_FORMAT_RGBA5551 = 0x35314152,
	WL_DISPMANX_FORMAT_BGRA5551 = 0x35314142,
	WL_DISPMANX_FORMAT_RGB565 = 0x36314752,
	WL_DISPMANX_FORMAT_BGR565 = 0x36314742,
	WL_DISPMANX_FORMAT_RGB888 = 0x34324752,
	WL_DISPMANX_FORMAT_BGR888 = 0x34324742,
	WL_DISPMANX_FORMAT_XRGB8888 = 0x34325258,
	WL_DISPMANX_FORMAT_XBGR8888 = 0x34324258,
	WL_DISPMANX_FORMAT_RGBX8888 = 0x34325852,
	WL_DISPMANX_FORMAT_BGRX8888 = 0x34325842,
	WL_DISPMANX_FORMAT_ARGB8888 = 0x34325241,
	WL_DISPMANX_FORMAT_ABGR8888 = 0x34324241,
	WL_DISPMANX_FORMAT_RGBA8888 = 0x34324152,
	WL_DISPMANX_FORMAT_BGRA8888 = 0x34324142,
	WL_DISPMANX_FORMAT_XRGB2101010 = 0x30335258,
	WL_DISPMANX_FORMAT_XBGR2101010 = 0x30334258,
	WL_DISPMANX_FORMAT_RGBX1010102 = 0x30335852,
	WL_DISPMANX_FORMAT_BGRX1010102 = 0x30335842,
	WL_DISPMANX_FORMAT_ARGB2101010 = 0x30335241,
	WL_DISPMANX_FORMAT_ABGR2101010 = 0x30334241,
	WL_DISPMANX_FORMAT_RGBA1010102 = 0x30334152,
	WL_DISPMANX_FORMAT_BGRA1010102 = 0x30334142,
	WL_DISPMANX_FORMAT_YUYV = 0x56595559,
	WL_DISPMANX_FORMAT_YVYU = 0x55595659,
	WL_DISPMANX_FORMAT_UYVY = 0x59565955,
	WL_DISPMANX_FORMAT_VYUY = 0x59555956,
	WL_DISPMANX_FORMAT_AYUV = 0x56555941,
	WL_DISPMANX_FORMAT_NV12 = 0x3231564e,
	WL_DISPMANX_FORMAT_NV21 = 0x3132564e,
	WL_DISPMANX_FORMAT_NV16 = 0x3631564e,
	WL_DISPMANX_FORMAT_NV61 = 0x3136564e,
	WL_DISPMANX_FORMAT_YUV410 = 0x39565559,
	WL_DISPMANX_FORMAT_YVU410 = 0x39555659,
	WL_DISPMANX_FORMAT_YUV411 = 0x31315559,
	WL_DISPMANX_FORMAT_YVU411 = 0x31315659,
	WL_DISPMANX_FORMAT_YUV420 = 0x32315559,
	WL_DISPMANX_FORMAT_YVU420 = 0x32315659,
	WL_DISPMANX_FORMAT_YUV422 = 0x36315559,
	WL_DISPMANX_FORMAT_YVU422 = 0x36315659,
	WL_DISPMANX_FORMAT_YUV444 = 0x34325559,
	WL_DISPMANX_FORMAT_YVU444 = 0x34325659,
};
#endif /* WL_DISPMANX_FORMAT_ENUM */

struct wl_dispmanx_interface {
	/**
	 * create_buffer - (none)
	 * @id: (none)
	 * @width: (none)
	 * @height: (none)
	 * @stride: (none)
	 * @buffer_height: (none)
	 * @format: (none)
	 */
	void (*create_buffer)(struct wl_client *client,
			      struct wl_resource *resource,
			      uint32_t id,
			      int32_t width,
			      int32_t height,
			      uint32_t stride,
			      uint32_t buffer_height,
			      uint32_t format);
	/**
	 * create_proxy_buffer - (none)
	 * @id: (none)
	 * @handle: (none)
	 * @width: (none)
	 * @height: (none)
	 * @format: (none)
	 */
	void (*create_proxy_buffer)(struct wl_client *client,
				    struct wl_resource *resource,
				    uint32_t id,
				    uint32_t handle,
				    int32_t width,
				    int32_t height,
				    uint32_t format);
};

#define WL_DISPMANX_FORMAT	0
#define WL_DISPMANX_BUFFER_ALLOCATED	1

static inline void
wl_dispmanx_send_format(struct wl_resource *resource_, uint32_t format)
{
	wl_resource_post_event(resource_, WL_DISPMANX_FORMAT, format);
}

static inline void
wl_dispmanx_send_buffer_allocated(struct wl_resource *resource_, struct wl_resource *buffer, uint32_t handle)
{
	wl_resource_post_event(resource_, WL_DISPMANX_BUFFER_ALLOCATED, buffer, handle);
}

#ifdef  __cplusplus
}
#endif

#endif

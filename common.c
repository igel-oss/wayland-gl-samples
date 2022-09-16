/*
 * Copyright © 2022 IGEL Co., Ltd.
 * Copyright © 2011 Benjamin Franzke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Tomohito Esaki <etom@igel.co.jp>
 *
 * Based on simple-egl.c in weston 10.0.92 by the following authors:
 *    Benjamin Franzke
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>

#include <linux/input.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "xdg-shell-client-protocol.h"
#include <sys/types.h>
#include <unistd.h>

#include "platform.h"
#include "common.h"

#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

struct window;
struct seat;

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *wm_base;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_touch *touch;
	struct wl_keyboard *keyboard;
	struct wl_shm *shm;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *default_cursor;
	struct wl_surface *cursor_surface;
	struct {
		EGLDisplay dpy;
		EGLContext ctx;
		EGLConfig conf;
	} egl;
	struct window *window;

	struct wl_list output_list; /* struct output::link */

	PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC swap_buffers_with_damage;
};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	struct geometry window_size;
	struct geometry logical_size;
	struct geometry buffer_size;
	int32_t buffer_scale;
	enum wl_output_transform buffer_transform;
	bool needs_buffer_geometry_update;

	struct wl_egl_window *native;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	EGLSurface egl_surface;
	int fullscreen, maximized, opaque, frame_sync;
	bool wait_for_configure;

	struct wl_list window_output_list; /* struct window_output::link */

	struct app_info *app;
};

struct output {
	struct display *display;
	struct wl_output *wl_output;
	uint32_t name;
	struct wl_list link; /* struct display::output_list */
	enum wl_output_transform transform;
	int32_t scale;
};

struct window_output {
	struct output *output;
	struct wl_list link; /* struct window::window_output_list */
};

static int running = 1;

static void
init_egl(struct display *display, struct window *window)
{
	static const struct {
		char *extension, *entrypoint;
	} swap_damage_ext_to_entrypoint[] = {
		{
			.extension = "EGL_EXT_swap_buffers_with_damage",
			.entrypoint = "eglSwapBuffersWithDamageEXT",
		},
		{
			.extension = "EGL_KHR_swap_buffers_with_damage",
			.entrypoint = "eglSwapBuffersWithDamageKHR",
		},
	};

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	const char *extensions;

	EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	EGLint major, minor, n, count, i = 0;
	EGLBoolean ret;

	if (window->opaque)
		config_attribs[9] = 0;

	display->egl.dpy =
		platform_get_egl_display(EGL_PLATFORM_WAYLAND_KHR,
					 display->display, NULL);
	assert(display->egl.dpy);

	ret = eglInitialize(display->egl.dpy, &major, &minor);
	assert(ret == EGL_TRUE);
	ret = eglBindAPI(EGL_OPENGL_ES_API);
	assert(ret == EGL_TRUE);

	if (!eglGetConfigs(display->egl.dpy, NULL, 0, &count) || count < 1)
		assert(0);

	ret = eglChooseConfig(display->egl.dpy, config_attribs, &display->egl.conf, 1, &n);
	assert(ret && n == 1);

	display->egl.ctx = eglCreateContext(display->egl.dpy,
					    display->egl.conf,
					    EGL_NO_CONTEXT, context_attribs);
	assert(display->egl.ctx);

	display->swap_buffers_with_damage = NULL;
	extensions = eglQueryString(display->egl.dpy, EGL_EXTENSIONS);
	if (extensions &&
	    check_egl_extension(extensions, "EGL_EXT_buffer_age")) {
		for (i = 0; i < (int) ARRAY_LENGTH(swap_damage_ext_to_entrypoint); i++) {
			if (check_egl_extension(extensions,
						swap_damage_ext_to_entrypoint[i].extension)) {
				/* The EXTPROC is identical to the KHR one */
				display->swap_buffers_with_damage =
					(PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC)
					eglGetProcAddress(swap_damage_ext_to_entrypoint[i].entrypoint);
				break;
			}
		}
	}

	if (display->swap_buffers_with_damage)
		printf("has EGL_EXT_buffer_age and %s\n", swap_damage_ext_to_entrypoint[i].extension);

}

static void
fini_egl(struct display *display)
{
	eglTerminate(display->egl.dpy);
	eglReleaseThread();
}

static int32_t
compute_buffer_scale(struct window *window)
{
	struct window_output *window_output;
	int32_t scale = 1;

	wl_list_for_each(window_output, &window->window_output_list, link) {
		if (window_output->output->scale > scale)
			scale = window_output->output->scale;
	}

	return scale;
}

static enum wl_output_transform
compute_buffer_transform(struct window *window)
{
	struct window_output *window_output;
	enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;

	wl_list_for_each(window_output, &window->window_output_list, link) {
		/* If the surface spans over multiple outputs the optimal
		 * transform value can be ambiguous. Thus just return the value
		 * from the oldest entered output.
		 */
		transform = window_output->output->transform;
		break;
	}

	return transform;
}

static void
update_buffer_geometry(struct window *window)
{
	enum wl_output_transform new_buffer_transform;
	int32_t new_buffer_scale;
	struct geometry new_buffer_size;

	new_buffer_transform = compute_buffer_transform(window);
	if (window->buffer_transform != new_buffer_transform) {
		window->buffer_transform = new_buffer_transform;
		wl_surface_set_buffer_transform(window->surface,
						window->buffer_transform);
	}

	new_buffer_scale = compute_buffer_scale(window);
	if (window->buffer_scale != new_buffer_scale) {
		window->buffer_scale = new_buffer_scale;
		wl_surface_set_buffer_scale(window->surface,
					    window->buffer_scale);
	}

	switch (window->buffer_transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		new_buffer_size.width = window->logical_size.width;
		new_buffer_size.height = window->logical_size.height;
		break;
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_270:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		new_buffer_size.width = window->logical_size.height;
		new_buffer_size.height = window->logical_size.width;
		break;
	}

	new_buffer_size.width *= window->buffer_scale;
	new_buffer_size.height *= window->buffer_scale;

	if (window->buffer_size.width != new_buffer_size.width ||
	    window->buffer_size.height != new_buffer_size.height) {
		window->buffer_size = new_buffer_size;
		if (window->native)
			wl_egl_window_resize(window->native,
					     window->buffer_size.width,
					     window->buffer_size.height, 0, 0);
	}

	window->needs_buffer_geometry_update = false;
}

static void
init_gl(struct window *window)
{
	EGLBoolean ret;

	if (window->needs_buffer_geometry_update)
		update_buffer_geometry(window);

	window->native = wl_egl_window_create(window->surface,
					      window->buffer_size.width,
					      window->buffer_size.height);
	window->egl_surface =
		platform_create_egl_surface(window->display->egl.dpy,
					    window->display->egl.conf,
					    window->native, NULL);

	ret = eglMakeCurrent(window->display->egl.dpy, window->egl_surface,
			     window->egl_surface, window->display->egl.ctx);
	assert(ret == EGL_TRUE);

	if (!window->frame_sync)
		eglSwapInterval(window->display->egl.dpy, 0);

	window->app->cb.init_gl(window->app->cb.user_data);
}

static void
handle_surface_configure(void *data, struct xdg_surface *surface,
			 uint32_t serial)
{
	struct window *window = data;

	xdg_surface_ack_configure(surface, serial);

	window->wait_for_configure = false;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_surface_configure
};

static void
handle_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
			  int32_t width, int32_t height,
			  struct wl_array *states)
{
	struct window *window = data;
	uint32_t *p;

	window->fullscreen = 0;
	window->maximized = 0;
	wl_array_for_each(p, states) {
		uint32_t state = *p;
		switch (state) {
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			window->fullscreen = 1;
			break;
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			window->maximized = 1;
			break;
		}
	}

	if (width > 0 && height > 0) {
		if (!window->fullscreen && !window->maximized) {
			window->window_size.width = width;
			window->window_size.height = height;
		}
		window->logical_size.width = width;
		window->logical_size.height = height;
	} else if (!window->fullscreen && !window->maximized) {
		window->logical_size = window->window_size;
	}

	window->needs_buffer_geometry_update = true;
}

static void
handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close,
};

static void
add_window_output(struct window *window, struct wl_output *wl_output)
{
	struct output *output;
	struct output *output_found = NULL;
	struct window_output *window_output;

	wl_list_for_each(output, &window->display->output_list, link) {
		if (output->wl_output == wl_output) {
			output_found = output;
			break;
		}
	}

	if (!output_found)
		return;

	window_output = malloc(sizeof *window_output);
	assert(window_output);
	window_output->output = output_found;

	wl_list_insert(window->window_output_list.prev, &window_output->link);
	window->needs_buffer_geometry_update = true;
}

static void
destroy_window_output(struct window *window, struct wl_output *wl_output)
{
	struct window_output *window_output;
	struct window_output *window_output_found = NULL;

	wl_list_for_each(window_output, &window->window_output_list, link) {
		if (window_output->output->wl_output == wl_output) {
			window_output_found = window_output;
			break;
		}
	}

	if (window_output_found) {
		wl_list_remove(&window_output_found->link);
		free(window_output_found);
		window->needs_buffer_geometry_update = true;
	}
}

static void
surface_enter(void *data,
	      struct wl_surface *wl_surface, struct wl_output *wl_output)
{
	struct window *window = data;

	add_window_output(window, wl_output);
}

static void
surface_leave(void *data,
	      struct wl_surface *wl_surface, struct wl_output *wl_output)
{
	struct window *window = data;

	destroy_window_output(window, wl_output);
}

static const struct wl_surface_listener surface_listener = {
	surface_enter,
	surface_leave
};

static void
create_surface(struct window *window)
{
	struct display *display = window->display;

	window->surface = wl_compositor_create_surface(display->compositor);
	wl_surface_add_listener(window->surface, &surface_listener, window);

	window->xdg_surface = xdg_wm_base_get_xdg_surface(display->wm_base,
							  window->surface);
	xdg_surface_add_listener(window->xdg_surface,
				 &xdg_surface_listener, window);

	window->xdg_toplevel =
		xdg_surface_get_toplevel(window->xdg_surface);
	xdg_toplevel_add_listener(window->xdg_toplevel,
				  &xdg_toplevel_listener, window);

	if (window->app->name)
		xdg_toplevel_set_title(window->xdg_toplevel, window->app->name);
	if (window->app->id)
		xdg_toplevel_set_app_id(window->xdg_toplevel, window->app->id);

	if (window->fullscreen)
		xdg_toplevel_set_fullscreen(window->xdg_toplevel, NULL);
	else if (window->maximized)
		xdg_toplevel_set_maximized(window->xdg_toplevel);

	window->wait_for_configure = true;
	wl_surface_commit(window->surface);
}

static void
destroy_surface(struct window *window)
{
	/* Required, otherwise segfault in egl_dri2.c: dri2_make_current()
	 * on eglReleaseThread(). */
	eglMakeCurrent(window->display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);

	platform_destroy_egl_surface(window->display->egl.dpy,
				     window->egl_surface);
	wl_egl_window_destroy(window->native);

	if (window->xdg_toplevel)
		xdg_toplevel_destroy(window->xdg_toplevel);
	if (window->xdg_surface)
		xdg_surface_destroy(window->xdg_surface);
	wl_surface_destroy(window->surface);
}

static void
redraw(struct window *window)
{
	struct display *display = window->display;
	struct wl_region *region;
	EGLint rect[4];
	EGLint buffer_age = 0;
	struct rect damage = {0};

	if (window->needs_buffer_geometry_update)
		update_buffer_geometry(window);

	window->app->cb.redraw(window->app->cb.user_data, &damage);

	if (display->swap_buffers_with_damage)
		eglQuerySurface(display->egl.dpy, window->egl_surface,
				EGL_BUFFER_AGE_EXT, &buffer_age);

	if (window->opaque || window->fullscreen) {
		region = wl_compositor_create_region(window->display->compositor);
		wl_region_add(region, 0, 0, INT32_MAX, INT32_MAX);
		wl_surface_set_opaque_region(window->surface, region);
		wl_region_destroy(region);
	} else {
		wl_surface_set_opaque_region(window->surface, NULL);
	}

	if (display->swap_buffers_with_damage && buffer_age > 0 &&
		damage.width > 0 && damage.height > 0) {
		rect[0] = damage.x;
		rect[1] = damage.y;
		rect[2] = damage.x + damage.width - 1;
		rect[3] = damage.y + damage.height -1;
		display->swap_buffers_with_damage(display->egl.dpy,
						  window->egl_surface,
						  rect, 1);
	} else {
		eglSwapBuffers(display->egl.dpy, window->egl_surface);
	}
}

static void
pointer_handle_enter(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface,
		     wl_fixed_t sx, wl_fixed_t sy)
{
	struct display *display = data;
	struct wl_buffer *buffer;
	struct wl_cursor *cursor = display->default_cursor;
	struct wl_cursor_image *image;

	if (display->window->fullscreen)
		wl_pointer_set_cursor(pointer, serial, NULL, 0, 0);
	else if (cursor) {
		image = display->default_cursor->images[0];
		buffer = wl_cursor_image_get_buffer(image);
		if (!buffer)
			return;
		wl_pointer_set_cursor(pointer, serial,
				      display->cursor_surface,
				      image->hotspot_x,
				      image->hotspot_y);
		wl_surface_attach(display->cursor_surface, buffer, 0, 0);
		wl_surface_damage(display->cursor_surface, 0, 0,
				  image->width, image->height);
		wl_surface_commit(display->cursor_surface);
	}
}

static void
pointer_handle_leave(void *data, struct wl_pointer *pointer,
		     uint32_t serial, struct wl_surface *surface)
{
}

static void
pointer_handle_motion(void *data, struct wl_pointer *pointer,
		      uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
}

static void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		      uint32_t serial, uint32_t time, uint32_t button,
		      uint32_t state)
{
	struct display *display = data;

	if (!display->window->xdg_toplevel)
		return;

	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED)
		xdg_toplevel_move(display->window->xdg_toplevel,
				  display->seat, serial);
}

static void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		    uint32_t time, uint32_t axis, wl_fixed_t value)
{
}

static const struct wl_pointer_listener pointer_listener = {
	pointer_handle_enter,
	pointer_handle_leave,
	pointer_handle_motion,
	pointer_handle_button,
	pointer_handle_axis,
};

static void
touch_handle_down(void *data, struct wl_touch *wl_touch,
		  uint32_t serial, uint32_t time, struct wl_surface *surface,
		  int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
	struct display *d = (struct display *)data;

	if (!d->wm_base)
		return;

	xdg_toplevel_move(d->window->xdg_toplevel, d->seat, serial);
}

static void
touch_handle_up(void *data, struct wl_touch *wl_touch,
		uint32_t serial, uint32_t time, int32_t id)
{
}

static void
touch_handle_motion(void *data, struct wl_touch *wl_touch,
		    uint32_t time, int32_t id, wl_fixed_t x_w, wl_fixed_t y_w)
{
}

static void
touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
}

static void
touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
}

static const struct wl_touch_listener touch_listener = {
	touch_handle_down,
	touch_handle_up,
	touch_handle_motion,
	touch_handle_frame,
	touch_handle_cancel,
};

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
		       uint32_t format, int fd, uint32_t size)
{
	/* Just so we don’t leak the keymap fd */
	close(fd);
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface,
		      struct wl_array *keys)
{
}

static void
keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
		      uint32_t serial, struct wl_surface *surface)
{
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
		    uint32_t serial, uint32_t time, uint32_t key,
		    uint32_t state)
{
	struct display *d = data;

	if (!d->wm_base)
		return;

	if (key == KEY_F11 && state) {
		if (d->window->fullscreen)
			xdg_toplevel_unset_fullscreen(d->window->xdg_toplevel);
		else
			xdg_toplevel_set_fullscreen(d->window->xdg_toplevel, NULL);
	} else if (key == KEY_ESC && state)
		running = 0;
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
			  uint32_t serial, uint32_t mods_depressed,
			  uint32_t mods_latched, uint32_t mods_locked,
			  uint32_t group)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,
	keyboard_handle_enter,
	keyboard_handle_leave,
	keyboard_handle_key,
	keyboard_handle_modifiers,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
			 enum wl_seat_capability caps)
{
	struct display *d = data;

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !d->pointer) {
		d->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(d->pointer, &pointer_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && d->pointer) {
		wl_pointer_destroy(d->pointer);
		d->pointer = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !d->keyboard) {
		d->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(d->keyboard, &keyboard_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && d->keyboard) {
		wl_keyboard_destroy(d->keyboard);
		d->keyboard = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !d->touch) {
		d->touch = wl_seat_get_touch(seat);
		wl_touch_set_user_data(d->touch, d);
		wl_touch_add_listener(d->touch, &touch_listener, d);
	} else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && d->touch) {
		wl_touch_destroy(d->touch);
		d->touch = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	seat_handle_capabilities,
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	xdg_wm_base_ping,
};

static void
display_handle_geometry(void *data,
			struct wl_output *wl_output,
			int32_t x, int32_t y,
			int32_t physical_width,
			int32_t physical_height,
			int32_t subpixel,
			const char *make,
			const char *model,
			int32_t transform)
{
	struct output *output = data;

	output->transform = transform;
	output->display->window->needs_buffer_geometry_update = true;
}

static void
display_handle_mode(void *data,
		    struct wl_output *wl_output,
		    uint32_t flags,
		    int32_t width,
		    int32_t height,
		    int32_t refresh)
{
}

static void
display_handle_done(void *data,
		     struct wl_output *wl_output)
{
}

static void
display_handle_scale(void *data,
		     struct wl_output *wl_output,
		     int32_t scale)
{
	struct output *output = data;

	output->scale = scale;
	output->display->window->needs_buffer_geometry_update = true;
}

static const struct wl_output_listener output_listener = {
	display_handle_geometry,
	display_handle_mode,
	display_handle_done,
	display_handle_scale
};

static void
display_add_output(struct display *d, uint32_t name)
{
	struct output *output;

	output = calloc(1, sizeof *output);
	assert(output);
	output->display = d;
	output->scale = 1;
	output->wl_output =
		wl_registry_bind(d->registry, name, &wl_output_interface, 2);
	output->name = name;
	wl_list_insert(d->output_list.prev, &output->link);

	wl_output_add_listener(output->wl_output, &output_listener, output);
}

static void
display_destroy_output(struct display *d, struct output *output)
{
	destroy_window_output(d->window, output->wl_output);
	wl_output_destroy(output->wl_output);
	wl_list_remove(&output->link);
	free(output);
}

static void
display_destroy_outputs(struct display *d)
{
	struct output *tmp;
	struct output *output;

	wl_list_for_each_safe(output, tmp, &d->output_list, link)
		display_destroy_output(d, output);
}

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t name, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface,
					 MIN(version, 4));
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		d->wm_base = wl_registry_bind(registry, name,
					      &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(d->wm_base, &wm_base_listener, d);
	} else if (strcmp(interface, "wl_seat") == 0) {
		d->seat = wl_registry_bind(registry, name,
					   &wl_seat_interface, 1);
		wl_seat_add_listener(d->seat, &seat_listener, d);
	} else if (strcmp(interface, "wl_shm") == 0) {
		d->shm = wl_registry_bind(registry, name,
					  &wl_shm_interface, 1);
		d->cursor_theme = wl_cursor_theme_load(NULL, 32, d->shm);
		if (!d->cursor_theme) {
			fprintf(stderr, "unable to load default theme\n");
			return;
		}
		d->default_cursor =
			wl_cursor_theme_get_cursor(d->cursor_theme, "left_ptr");
		if (!d->default_cursor) {
			fprintf(stderr, "unable to load default left pointer\n");
			// TODO: abort ?
		}
	} else if (strcmp(interface, "wl_output") == 0 && version >= 2) {
		display_add_output(d, name);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
	struct display *d = data;
	struct output *output;

	wl_list_for_each(output, &d->output_list, link) {
		if (output->name == name) {
			display_destroy_output(d, output);
			break;
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void
signal_int(int signum)
{
	running = 0;
}

static void
usage(int error_code, char *name)
{
	fprintf(stderr, "Usage: %s [OPTIONS]\n\n"
		"  -f\tRun in fullscreen mode\n"
		"  -m\tRun in maximized mode\n"
		"  -o\tCreate an opaque surface\n"
		"  -b\tDon't sync to compositor redraw (eglSwapInterval 0)\n"
		"  -h\tThis help text\n\n", name);

	exit(error_code);
}

void
app_main(int argc, char **argv, struct app_info *app)
{
	struct sigaction sigint;
	struct display display = { 0 };
	struct window  window  = { 0 };
	int i, ret = 0;

	if (!app || !app->cb.init_gl || !app->cb.redraw)
		return;

	window.display = &display;
	display.window = &window;
	window.buffer_size.width  = app->win_width;
	window.buffer_size.height = app->win_height;
	window.window_size = window.buffer_size;
	window.buffer_scale = 1;
	window.buffer_transform = WL_OUTPUT_TRANSFORM_NORMAL;
	window.needs_buffer_geometry_update = false;
	window.frame_sync = 1;

	wl_list_init(&display.output_list);
	wl_list_init(&window.window_output_list);

	window.app = app;

	for (i = 1; i < argc; i++) {
		if (strcmp("-f", argv[i]) == 0)
			window.fullscreen = 1;
		else if (strcmp("-m", argv[i]) == 0)
			window.maximized = 1;
		else if (strcmp("-o", argv[i]) == 0)
			window.opaque = 1;
		else if (strcmp("-b", argv[i]) == 0)
			window.frame_sync = 0;
		else if (strcmp("-h", argv[i]) == 0)
			usage(EXIT_SUCCESS, argv[0]);
		else
			usage(EXIT_FAILURE, argv[0]);
	}

	display.display = wl_display_connect(NULL);
	assert(display.display);

	display.registry = wl_display_get_registry(display.display);
	wl_registry_add_listener(display.registry,
				 &registry_listener, &display);

	wl_display_roundtrip(display.display);

	if (!display.wm_base) {
		fprintf(stderr, "xdg-shell support required. simple-egl exiting\n");
		goto out_no_xdg_shell;
	}

	init_egl(&display, &window);
	create_surface(&window);

	/* we already have wait_for_configure set after create_surface() */
	while (running && ret != -1 && window.wait_for_configure) {
		ret = wl_display_dispatch(display.display);

		/* wait until xdg_surface::configure acks the new dimensions */
		if (window.wait_for_configure)
			continue;

		init_gl(&window);
	}

	display.cursor_surface =
		wl_compositor_create_surface(display.compositor);

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	while (running && ret != -1) {
		ret = wl_display_dispatch_pending(display.display);
		redraw(&window);
	}

	fprintf(stderr, "%s exiting\n", app->name);

	window.app->cb.deinit_gl(window.app->cb.user_data);

	destroy_surface(&window);
	fini_egl(&display);

	wl_surface_destroy(display.cursor_surface);
out_no_xdg_shell:
	display_destroy_outputs(&display);

	if (display.cursor_theme)
		wl_cursor_theme_destroy(display.cursor_theme);

	if (display.shm)
		wl_shm_destroy(display.shm);

	if (display.pointer)
		wl_pointer_destroy(display.pointer);

	if (display.keyboard)
		wl_keyboard_destroy(display.keyboard);

	if (display.touch)
		wl_touch_destroy(display.touch);

	if (display.seat)
		wl_seat_destroy(display.seat);

	if (display.wm_base)
		xdg_wm_base_destroy(display.wm_base);

	if (display.compositor)
		wl_compositor_destroy(display.compositor);

	wl_registry_destroy(display.registry);
	wl_display_flush(display.display);
	wl_display_disconnect(display.display);
}

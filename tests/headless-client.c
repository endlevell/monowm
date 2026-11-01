#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "test-xdg-shell.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static int configured, width, height;

static void ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
	xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_listener = {.ping = ping};

static void global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (!strcmp(interface, "wl_compositor"))
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	else if (!strcmp(interface, "wl_shm"))
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (!strcmp(interface, "xdg_wm_base")) {
		wm = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wm, &wm_listener, NULL);
	}
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {.global = global, .global_remove = removed};

static void configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
	xdg_surface_ack_configure(surface, serial);
	configured = 1;
}
static const struct xdg_surface_listener surface_listener = {.configure = configure};
static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
		int32_t w, int32_t h, struct wl_array *states)
{
	width = w;
	height = h;
}
static void close_toplevel(void *data, struct xdg_toplevel *toplevel) {}
static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure, .close = close_toplevel,
};

int main(int argc, char **argv)
{
	struct wl_display *display = wl_display_connect(NULL);
	assert(display);
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	assert(wl_display_roundtrip(display) >= 0);
	assert(compositor && shm && wm);
	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(wm, surface);
	xdg_surface_add_listener(xdg, &surface_listener, NULL);
	struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg);
	xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
	xdg_toplevel_set_app_id(toplevel, "mono-regression");
	wl_surface_commit(surface);
	while (!configured)
		assert(wl_display_dispatch(display) >= 0);
	int fd = memfd_create("mono-regression", MFD_CLOEXEC);
	size_t bytes = 320 * 240 * 4;
	assert(fd >= 0 && !ftruncate(fd, bytes));
	void *pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	assert(pixels != MAP_FAILED);
	memset(pixels, 255, bytes);
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, bytes);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, 320, 240, 320 * 4, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, 320, 240);
	wl_surface_commit(surface);
	assert(wl_display_roundtrip(display) >= 0);
	if (argc > 1 && !strcmp(argv[1], "fullscreen")) {
		for (int i = 0; i < 2; i++) {
			xdg_toplevel_set_fullscreen(toplevel, NULL);
			assert(wl_display_roundtrip(display) >= 0);
			assert(wl_display_roundtrip(display) >= 0);
			wl_surface_commit(surface);
			assert(wl_display_roundtrip(display) >= 0);
		}
		xdg_toplevel_unset_fullscreen(toplevel);
		assert(wl_display_roundtrip(display) >= 0);
		assert(wl_display_roundtrip(display) >= 0);
		assert(width == 320 && height == 240);
	}
	xdg_toplevel_destroy(toplevel);
	xdg_surface_destroy(xdg);
	wl_surface_destroy(surface);
	wl_buffer_destroy(buffer);
	wl_shm_pool_destroy(pool);
	xdg_wm_base_destroy(wm);
	wl_shm_destroy(shm);
	wl_compositor_destroy(compositor);
	wl_registry_destroy(registry);
	assert(wl_display_roundtrip(display) >= 0);
	wl_display_disconnect(display);
	munmap(pixels, bytes);
	close(fd);
	return 0;
}

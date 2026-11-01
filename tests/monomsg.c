/* Exercise client request construction without needing a desktop session. */
#define main monomsg_main
#define wl_proxy_marshal_flags test_proxy_marshal_flags
#define wl_proxy_get_version test_proxy_get_version
#define wl_proxy_add_listener test_proxy_add_listener
#define wl_display_connect test_display_connect
#define wl_display_flush test_display_flush
#include "../tools/monomsg/src/main.c"
#undef main
#include <assert.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <unistd.h>

static uint32_t requested_mask, requested_toggle;
static int subscriptions;

uint32_t test_proxy_get_version(struct wl_proxy *proxy) { return 2; }
int test_proxy_add_listener(struct wl_proxy *proxy, void (**impl)(void), void *data) { return 0; }

struct wl_proxy *test_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
		const struct wl_interface *interface, uint32_t version, uint32_t flags, ...)
{
	if (interface == &zdwl_ipc_output_v2_interface) {
		subscriptions++;
	} else if (opcode == ZDWL_IPC_OUTPUT_V2_SET_TAGS) {
		va_list args;
		va_start(args, flags);
		requested_mask = va_arg(args, uint32_t);
		requested_toggle = va_arg(args, uint32_t);
		va_end(args);
	}
	return (void *)1;
}

struct wl_display *test_display_connect(const char *name)
{
	assert(mode == WATCH && Fflag);
	_exit(0);
}

int test_display_flush(struct wl_display *display)
{
	assert(requested_mask == 1 && requested_toggle == 0);
	return 0;
}

int main(void)
{
	pid_t pid = fork();
	assert(pid >= 0);
	if (!pid) {
		mode = SET;
		tflag = 1;
		seltags = 1u << 4;
		tagcount = 9;
		tagset = "!0";
		focused_output = strdup("HEADLESS-1");
		dwl_ipc_output_frame("HEADLESS-1", (void *)1);
		assert(requests_sent && requested_mask == 1 && requested_toggle == 0);
		requested_mask = 123;
		dwl_ipc_output_frame("HEADLESS-1", (void *)1);
		assert(requested_mask == 123); /* state replies must not resend changes */
		_exit(0);
	}
	int status;
	assert(waitpid(pid, &status, 0) == pid);
	assert(WIFEXITED(status) && !WEXITSTATUS(status));
	mode = WATCH;
	dwl_ipc_output_active("HEADLESS-1", (void *)1, 1);
	assert(!output_name && !strcmp(focused_output, "HEADLESS-1"));
	dwl_ipc_manager = (void *)1;
	wl_output_name(NULL, (void *)1, "HEADLESS-2");
	assert(subscriptions == 1);
	free(focused_output);
	focused_output = NULL;
	pid = fork();
	assert(pid >= 0);
	if (!pid) {
		mode = NONE;
		char *args[] = {"monomsg", "-w", "-F", NULL};
		monomsg_main(3, args);
		_exit(1);
	}
	assert(waitpid(pid, &status, 0) == pid);
	assert(WIFEXITED(status) && !WEXITSTATUS(status));
	puts("monomsg regressions passed");
	return 0;
}

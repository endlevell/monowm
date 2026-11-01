#include "dwl-ipc-unstable-v2-protocol.h"

/* Forward declarations for dwl.c static functions */

static void arrange(Monitor *m);
static void focusclient(Client *c, int lift);
static Client *focustop(Monitor *m);
static void printstatus(void);

/* globals  */

static struct wl_global *ipc_global = NULL;
/* Output subscriptions outlive the manager that created them. */
static struct wl_list ipc_outputs; /* IpcOutput.link; independent of managers */

typedef struct {
	struct wl_resource     *resource;
} IpcManager;

typedef struct {
	struct wl_resource     *resource;
	Monitor                *mon;
	struct wl_list          link;     /* ipc_outputs */
} IpcOutput;

/* helpers  */

/* Send the full state of monitor m to one IpcOutput resource */
static void
ipc_send_output_state(IpcOutput *ipc_out)
{
	Monitor *m = ipc_out->mon;
	struct wl_resource *res = ipc_out->resource;
	Client *c = m->status.focused;
	uint32_t urg = m->status.urgent, sel_tags = m->status.selected;
	int tag, focused;

	/* active */
	zdwl_ipc_output_v2_send_active(res, m == selmon ? 1 : 0);

	/* per-tag state */
	for (tag = 0; tag < TAGCOUNT; tag++) {
		uint32_t mask   = 1u << tag;
		uint32_t state  = ZDWL_IPC_OUTPUT_V2_TAG_STATE_NONE;
		uint32_t clients_on_tag = 0;

		/* Bitmask, as in dwl: a tag can be both active and urgent */
		if (m->tagset[m->seltags] & mask)
			state |= ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE;
		if (urg & mask)
			state |= ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT;
		clients_on_tag = m->status.counts[tag];

		focused = (sel_tags & mask) ? 1 : 0;
		zdwl_ipc_output_v2_send_tag(res, tag, state, clients_on_tag, focused);
	}

	/* layout index */
	{
		uint32_t i;
		for (i = 0; i < LENGTH(layouts); i++)
			if (&layouts[i] == m->lt[m->sellt])
				break;
		zdwl_ipc_output_v2_send_layout(res, i);
	}

	/* layout symbol */
	zdwl_ipc_output_v2_send_layout_symbol(res, m->ltsymbol);

	/* title/appid/fullscreen/floating */
	if (c) {
		zdwl_ipc_output_v2_send_title(res, client_get_title(c));
		zdwl_ipc_output_v2_send_appid(res, client_get_appid(c));
		if (wl_resource_get_version(res) >= 2) {
			zdwl_ipc_output_v2_send_fullscreen(res, c->isfullscreen ? 1 : 0);
			zdwl_ipc_output_v2_send_floating(res, c->isfloating ? 1 : 0);
		}
	} else {
		zdwl_ipc_output_v2_send_title(res, "");
		zdwl_ipc_output_v2_send_appid(res, "");
		if (wl_resource_get_version(res) >= 2) {
			zdwl_ipc_output_v2_send_fullscreen(res, 0);
			zdwl_ipc_output_v2_send_floating(res, 0);
		}
	}

	/* frame — signals end of this batch of events */
	zdwl_ipc_output_v2_send_frame(res);
}

/* Called instead of (or alongside) the old printstatus() */
static void
ipc_printstatus(void)
{
	IpcOutput *out;
	wl_list_for_each(out, &ipc_outputs, link)
		if (out->mon)
			ipc_send_output_state(out);
}

/* Detach outputs bound to a monitor that is being freed; their requests
 * become no-ops until the client destroys them */
static void
ipc_cleanupmon(Monitor *m)
{
	IpcOutput *out;
	wl_list_for_each(out, &ipc_outputs, link)
		if (out->mon == m)
			out->mon = NULL;
}

/* zdwl_ipc_output_v2 requests */

static void
ipc_output_handle_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
ipc_output_handle_set_tags(struct wl_client *client, struct wl_resource *resource,
		uint32_t tagmask, uint32_t toggle_tagset)
{
	IpcOutput *out = wl_resource_get_user_data(resource);
	Monitor *m = out->mon;
	if (!m || !(tagmask &= TAGMASK)) return;

	if (toggle_tagset)
		m->seltags ^= 1;
	m->tagset[m->seltags] = tagmask & TAGMASK;
	focusclient(focustop(m), 1);
	arrange(m);
	printstatus();
}

static void
ipc_output_handle_set_client_tags(struct wl_client *client,
		struct wl_resource *resource, uint32_t and_tags, uint32_t xor_tags)
{
	IpcOutput *out = wl_resource_get_user_data(resource);
	Monitor *m = out->mon;
	Client *sel;
	uint32_t newtags;
	if (!m) return;

	sel = focustop(m);
	if (!sel) return;

	newtags = (sel->tags & and_tags) ^ xor_tags;
	settags(sel, newtags);
}

static void
ipc_output_handle_set_layout(struct wl_client *client,
		struct wl_resource *resource, uint32_t idx)
{
	IpcOutput *out = wl_resource_get_user_data(resource);
	Monitor *m = out->mon;
	if (!m || idx >= LENGTH(layouts)) return;

	m->lt[m->sellt] = &layouts[idx];
	snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", layouts[idx].symbol);
	arrange(m);
	printstatus();
}

static const struct zdwl_ipc_output_v2_interface ipc_output_impl = {
	.release          = ipc_output_handle_release,
	.set_tags         = ipc_output_handle_set_tags,
	.set_client_tags  = ipc_output_handle_set_client_tags,
	.set_layout       = ipc_output_handle_set_layout,
};

static void
ipc_output_destroy(struct wl_resource *resource)
{
	IpcOutput *out = wl_resource_get_user_data(resource);
	wl_list_remove(&out->link);
	free(out);
}

/* zdwl_ipc_manager_v2 requests */

static void
ipc_manager_handle_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
ipc_manager_handle_get_output(struct wl_client *client, struct wl_resource *resource,
		uint32_t id, struct wl_resource *output_resource)
{
	struct wlr_output *wlr_out = wlr_output_from_resource(output_resource);
	Monitor *m = (wlr_out && wlr_out->data) ? wlr_out->data : NULL;

	IpcOutput *out = ecalloc(1, sizeof(*out));
	out->mon = m;
	out->resource = wl_resource_create(client, &zdwl_ipc_output_v2_interface,
			wl_resource_get_version(resource), id);
	if (!out->resource) {
		free(out);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(out->resource, &ipc_output_impl,
			out, ipc_output_destroy);
	wl_list_insert(&ipc_outputs, &out->link);

	if (m) {
		status_collect(m, &m->status);
		ipc_send_output_state(out);
	}
}

static const struct zdwl_ipc_manager_v2_interface ipc_manager_impl = {
	.release    = ipc_manager_handle_release,
	.get_output = ipc_manager_handle_get_output,
};

static void
ipc_manager_destroy(struct wl_resource *resource)
{
	IpcManager *mgr = wl_resource_get_user_data(resource);
	free(mgr);
}

/* global bind */

static void
ipc_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	IpcManager *mgr = ecalloc(1, sizeof(*mgr));
	mgr->resource = wl_resource_create(client, &zdwl_ipc_manager_v2_interface,
			version, id);
	if (!mgr->resource) {
		free(mgr);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(mgr->resource, &ipc_manager_impl,
			mgr, ipc_manager_destroy);

	/* Announce tags and layouts */
	zdwl_ipc_manager_v2_send_tags(mgr->resource, TAGCOUNT);
	for (uint32_t i = 0; i < LENGTH(layouts); i++)
		zdwl_ipc_manager_v2_send_layout(mgr->resource, layouts[i].symbol);
}

static void
ipc_init(void)
{
	wl_list_init(&ipc_outputs);
	ipc_global = wl_global_create(dpy, &zdwl_ipc_manager_v2_interface,
			2, NULL, ipc_bind);
}

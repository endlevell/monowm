/*
 * Animations: windows opening and closing, layout moves, workspace slides,
 * smooth canvas zoom and pan glide.
 *
 * Each animation keeps its start time and derives its current value from the
 * time elapsed, so ticking more than once per frame (once per output) is
 * harmless. rendermon() ticks before committing and keeps scheduling frames
 * while anything still moves. A start time of 0 means "not animating".
 */

#define ANIM_OPEN_SCALE  0.8   /* size windows pop in from */
#define ANIM_CLOSE_SCALE 0.8   /* match the opening pop's scale */
#define ANIM_GLIDE_MS    325.0 /* pan glide velocity decay time constant */
#define ANIM_GLIDE_MIN   0.02  /* canvas units per ms below which a glide stops */
#define CANVAS_OVERVIEW_ZOOM 0.6
#define CANVAS_PARALLAX_OVERSCAN 0.12

/* Cubic bezier easing from (0,0) to (1,1) through (x1,y1) and (x2,y2), as in
 * CSS and Hyprland */
typedef struct {
	double x1, y1, x2, y2;
} Bezier;

static const Bezier emphasized_decel = {0.05, 0.7, 0.1, 1.0};
static const Bezier menu_decel       = {0.1, 1.0, 0.0, 1.0};
static const Bezier standard_decel   = {0.0, 0.0, 0.0, 1.0};

enum { ANIM_OPEN, ANIM_CLOSE, ANIM_MOVE, ANIM_WORKSPACE, ANIM_ZOOM };

/* Duration at speed 1 and curve of each kind of animation */
static const struct {
	double ms;
	const Bezier *curve;
} anim_kinds[] = {
	[ANIM_OPEN]      = { 300, &emphasized_decel },
	[ANIM_CLOSE]     = { 300, &emphasized_decel },
	[ANIM_MOVE]      = { 300, &emphasized_decel },
	[ANIM_WORKSPACE] = { 700, &menu_decel },
	[ANIM_ZOOM]      = { 300, &standard_decel },
};

/* Keep a displayed frame while its client closes or moves to a hidden tag. */
typedef struct {
	struct wlr_scene_node *node;
	int x, y, width, height;
	float color[4]; /* rects */
	float opacity;  /* buffers */
	struct wlr_fbox src_box;
} SnapshotPart;

typedef struct {
	struct wl_list link;
	struct wlr_scene_tree *tree;
	struct wl_array parts; /* SnapshotPart */
	double cx, cy;         /* tree-local point it shrinks toward */
	uint32_t start;
	int animation;
	Monitor *mon;
	uint32_t source_tags, target_tags; /* workspace transfers */
	int x, slide_to;
	Client *client;                  /* live owner of a workspace transfer */
	struct wlr_box frame;            /* full displayed frame before monitor clipping */
	double scale, opacity;
} Snapshot;

static struct wl_list snapshots;
static int snapshot_render(Snapshot *s, uint32_t now);
static void anim_cancel_transfer(Client *c);

static uint32_t
anim_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void
anim_schedule(void)
{
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		if (m->wlr_output->enabled)
			wlr_output_schedule_frame(m->wlr_output);
}

static int
anim_enabled(void)
{
	return cfg.animations.enabled && cfg.animations.speed > 0;
}

/* Start time for a new animation, or 0 when animations are off */
static uint32_t
anim_start(void)
{
	uint32_t now;
	if (!anim_enabled())
		return 0;
	anim_schedule();
	now = anim_now();
	return now ? now : 1;
}

/* One coordinate of the bezier at parameter s */
static double
bezier_at(double p1, double p2, double s)
{
	double u = 1.0 - s;
	return 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s;
}

/* Eased value at time fraction t: find s with x(s) = t, return y(s). x is
 * monotonic for control points in [0, 1], so bisection always converges. */
static double
bezier_ease(const Bezier *b, double t)
{
	double lo = 0.0, hi = 1.0, s = t;
	for (int i = 0; i < 24; i++) {
		s = (lo + hi) / 2.0;
		if (bezier_at(b->x1, b->x2, s) < t)
			lo = s;
		else
			hi = s;
	}
	return bezier_at(b->y1, b->y2, s);
}

/* Eased progress of an animation of the given kind that began at start;
 * exactly 1.0 once it has finished */
static double
anim_progress(int kind, uint32_t start, uint32_t now)
{
	double t;
	if (!start || !anim_enabled())
		return 1.0;
	t = (double)(uint32_t)(now - start) * cfg.animations.speed / anim_kinds[kind].ms;
	if (t >= 1.0)
		return 1.0;
	return bezier_ease(anim_kinds[kind].curve, t);
}

static int
anim_lerp(int a, int b, double t)
{
	return a + (int)round((b - a) * t);
}

/* The box c is displayed at: c->geom, or on its way there */
static struct wlr_box
client_displayed_geom(Client *c, uint32_t now)
{
	double t = anim_progress(ANIM_MOVE, c->geom_start, now);
	if (t >= 1.0)
		return c->geom;
	return (struct wlr_box){
		.x = anim_lerp(c->geom_from.x, c->geom.x, t),
		.y = anim_lerp(c->geom_from.y, c->geom.y, t),
		.width = anim_lerp(c->geom_from.width, c->geom.width, t),
		.height = anim_lerp(c->geom_from.height, c->geom.height, t),
	};
}

static double
client_frame_scale(Client *c, uint32_t now)
{
	double from = c->geom_scale_from > 0 ? c->geom_scale_from : 1.0;
	double t = anim_progress(ANIM_MOVE, c->geom_start, now);
	return from + (1.0 - from) * t;
}

/* Lay out c's borders and surface for a w x h frame at its tree origin.
 * Outer ring sits flush with the frame's outer edge, the inner ring is inset
 * by obw, and the surface by the full bw = ibw + obw. */
static void
client_place(Client *c, int w, int h, const struct wlr_box *content)
{
	unsigned int ib = c->ibw, ob = c->obw;
	struct wlr_box clip;

	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);

	wlr_scene_rect_set_size(c->inner_border[0], MAX(1, w - 2 * (int)ob), (int)ib);
	wlr_scene_node_set_position(&c->inner_border[0]->node, ob, ob);
	wlr_scene_rect_set_size(c->inner_border[1], MAX(1, w - 2 * (int)ob), (int)ib);
	wlr_scene_node_set_position(&c->inner_border[1]->node, ob, MAX(0, h - (int)ob - (int)ib));
	wlr_scene_rect_set_size(c->inner_border[2], (int)ib, MAX(1, h - 2 * (int)ob));
	wlr_scene_node_set_position(&c->inner_border[2]->node, ob, ob);
	wlr_scene_rect_set_size(c->inner_border[3], (int)ib, MAX(1, h - 2 * (int)ob));
	wlr_scene_node_set_position(&c->inner_border[3]->node, MAX(0, w - (int)ob - (int)ib), ob);

	wlr_scene_rect_set_size(c->outer_border[0], MAX(1, w), (int)ob);
	wlr_scene_node_set_position(&c->outer_border[0]->node, 0, 0);
	wlr_scene_rect_set_size(c->outer_border[1], MAX(1, w), (int)ob);
	wlr_scene_node_set_position(&c->outer_border[1]->node, 0, MAX(0, h - (int)ob));
	wlr_scene_rect_set_size(c->outer_border[2], (int)ob, MAX(1, h - 2 * (int)ob));
	wlr_scene_node_set_position(&c->outer_border[2]->node, 0, ob);
	wlr_scene_rect_set_size(c->outer_border[3], (int)ob, MAX(1, h - 2 * (int)ob));
	wlr_scene_node_set_position(&c->outer_border[3]->node, MAX(0, w - (int)ob), ob);

	/* Clip in native surface coordinates before stretching the content. */
	client_get_clip(c, &clip);
	clip.width = content->width;
	clip.height = content->height;
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

static void
set_buffer_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	wlr_scene_buffer_set_opacity(buffer, *(float *)data);
}

static void
client_set_opacity(Client *c, double opacity)
{
	float o = (float)opacity;
	if (opacity == 1.0 && c->opacity == 1.0)
		return;
	c->opacity = opacity;
	wlr_scene_node_for_each_buffer(&c->scene_surface->node, set_buffer_opacity, &o);
	client_set_border_color(c, c->inner_color, c->outer_color);
}

/* Push what c should display right now to the scene: position, frame size,
 * scale and opacity. Canvas placement, layout moves, opening and workspace
 * slides all combine here. */
void
client_render(Client *c)
{
	Monitor *m = c->mon;
	uint32_t now = anim_now();
	struct wlr_box box, content;
	double scale = 1.0, opacity = 1.0, t, s;
	int x, y;

	if (!m || !c->scene)
		return;

	box = client_displayed_geom(c, now);
	if (anim_progress(ANIM_MOVE, c->geom_start, now) >= 1.0)
		c->geom_start = 0;
	scale = client_frame_scale(c, now);
	/* Interpolate the frame in screen/native-canvas units while retaining
	 * optical border thickness through a canvas mode change. */
	box.width = MAX(1, (int)round(box.width / scale));
	box.height = MAX(1, (int)round(box.height / scale));
	if (canvas_active(c)) {
		box.x = m->m.x + (int)round((c->world_x - m->m.x - m->canvas_x) * m->zoom);
		box.y = m->m.y + (int)round((c->world_y - m->m.y - m->canvas_y) * m->zoom);
		scale *= m->zoom;
	}
	client_get_geometry(c, &content);
	if (content.width <= 0)
		content.width = MAX(1, box.width - 2 * (int)c->bw);
	if (content.height <= 0)
		content.height = MAX(1, box.height - 2 * (int)c->bw);
	c->content_scale_x = (double)MAX(1, box.width - 2 * (int)c->bw) / content.width;
	c->content_scale_y = (double)MAX(1, box.height - 2 * (int)c->bw) / content.height;
	c->rendered = (struct wlr_box){box.x, box.y,
			MAX(1, (int)round(box.width * scale)), MAX(1, (int)round(box.height * scale))};
	x = box.x;
	y = box.y;

	if (c->pop_start) {
		t = anim_progress(ANIM_OPEN, c->pop_start, now);
		s = ANIM_OPEN_SCALE + (1.0 - ANIM_OPEN_SCALE) * t;
		/* Grow from the center of the frame */
		x += (int)round(box.width * scale * (1.0 - s) / 2.0);
		y += (int)round(box.height * scale * (1.0 - s) / 2.0);
		scale *= s;
		opacity = t;
		if (t >= 1.0)
			c->pop_start = 0;
	}

	if (c->slide_start) {
		t = anim_progress(ANIM_WORKSPACE, c->slide_start, now);
		x += anim_lerp(c->slide_from, c->slide_to, t);
		y += anim_lerp(c->slide_from_y, 0, t);
		opacity *= c->slide_opacity_from + (1.0 - c->slide_opacity_from) * t;
		if (t >= 1.0) {
			c->slide_start = 0;
			if (c->leaving) {
				c->leaving = 0;
				wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			}
		}
	}

	/* Scaling rewrites node geometry, so lay out unscaled first */
	canvas_restore(c);
	client_place(c, box.width, box.height, &content);
	wlr_scene_node_set_position(&c->scene->node, x, y);
	if (scale != 1.0 || c->content_scale_x != 1.0 || c->content_scale_y != 1.0
			|| c->slide_start)
		canvas_apply(c, scale);
	client_set_opacity(c, opacity);
}

/* Start c's opening animation */
static void
anim_open(Client *c)
{
	c->pop_start = anim_start();
	client_render(c);
}

static void
anim_canvas_leave(Client *c)
{
	struct wlr_box from = c->rendered;
	double scale = c->mon->zoom * client_frame_scale(c, anim_now());
	int animate = c->incanvas && !c->isfullscreen && !wlr_box_empty(&from);

	canvas_leave(c);
	if (animate && (c->geom_start = anim_start())) {
		c->geom_from = from;
		c->geom_scale_from = scale;
		client_render(c);
	}
}

/* Slide m's windows when its view switches from tags old to new: windows
 * on the new tags come in from the side of the higher tag, the others leave
 * the opposite way. Windows on both stay put. */
static void
anim_view(Monitor *m, uint32_t old, uint32_t new)
{
	uint32_t start, now = anim_now();
	int dir, off, entering;
	Client *c;

	if (!old || !new || !(start = anim_start()))
		return;
	dir = __builtin_ctz(new) > __builtin_ctz(old) ? 1 : -1;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || !(c->tags & old) == !(c->tags & new))
			continue;
		entering = (c->tags & new) != 0;
		c->slide_from_y = c->slide_start
				? anim_lerp(c->slide_from_y, 0,
						anim_progress(ANIM_WORKSPACE, c->slide_start, now)) : 0;
		c->slide_opacity_from = c->slide_start
				? c->slide_opacity_from + (1.0 - c->slide_opacity_from)
						* anim_progress(ANIM_WORKSPACE, c->slide_start, now) : 1.0;
		/* Reverse from wherever a window is if it was already sliding */
		off = c->slide_start
				? anim_lerp(c->slide_from, c->slide_to, anim_progress(ANIM_WORKSPACE, c->slide_start, now))
				: entering ? dir * m->m.width : 0;
		c->slide_from = off;
		c->slide_to = entering ? 0 : -dir * m->m.width;
		c->leaving = !entering;
		c->slide_start = start;
	}
}

/* Canvas zoom keeping (zoom_wx, zoom_wy) under the anchor (zoom_ax, zoom_ay) */
static void
zoom_apply(Monitor *m, double zoom)
{
	double x = m->zoom_wx - m->zoom_ax / zoom;
	double y = m->zoom_wy - m->zoom_ay / zoom;
	/* Anchored zoom changes the camera origin without a pan gesture. Rebase
	 * its wallpaper sample so this doesn't add a second settling animation. */
	m->backdrop_camera_x += x - m->canvas_x;
	m->backdrop_camera_y += y - m->canvas_y;
	m->zoom = zoom;
	m->canvas_x = x;
	m->canvas_y = y;
	canvas_layout(m);
}

/* Zoom m's canvas to zoom around the cursor */
static void
anim_zoom_at(Monitor *m, double zoom, double x, double y)
{
	m->pan_glide = 0;
	m->zoom_ax = x - m->m.x;
	m->zoom_ay = y - m->m.y;
	m->zoom_wx = m->canvas_x + m->zoom_ax / m->zoom;
	m->zoom_wy = m->canvas_y + m->zoom_ay / m->zoom;
	m->zoom_from = m->zoom;
	m->zoom_to = zoom;
	if (!(m->zoom_start = anim_start()))
		zoom_apply(m, zoom);
}

static void
anim_zoom(Monitor *m, double zoom)
{
	anim_zoom_at(m, zoom, cursor->x, cursor->y);
}

static void
anim_backdrop(Monitor *m, int canvas)
{
	double target = canvas ? 1.0 : 0.0;
	if (m->backdrop_to == target && (m->backdrop_start || m->backdrop_mix == target))
		return;
	m->backdrop_from = m->backdrop_mix;
	m->backdrop_to = target;
	if (!(m->backdrop_start = anim_start()))
		m->backdrop_mix = target;
	backdrop_update(m);
}

/* Use local camera movement, rather than an absolute world coordinate that
 * saturates on an infinite canvas. Let the wallpaper settle after navigation. */
static int
backdrop_motion(Monitor *m, uint32_t now)
{
	int navigating = (cursor_mode == CurCanvasPan && panmon == m)
			|| m->pan_glide || m->zoom_start;
	if (!m->backdrop_last || m->backdrop_mix <= 0 || !anim_enabled()) {
		m->backdrop_offset_x = m->backdrop_offset_y = 0;
	} else {
		double dt = (double)(uint32_t)(now - m->backdrop_last);
		/* About 95% of the return happens within one zoom-animation duration. */
		double decay = navigating ? 1.0
				: exp(-3.0 * dt * cfg.animations.speed / anim_kinds[ANIM_ZOOM].ms);
		double dx = 2.0 * (m->canvas_x - m->backdrop_camera_x) * m->zoom / MAX(1, m->m.width);
		double dy = 2.0 * (m->canvas_y - m->backdrop_camera_y) * m->zoom / MAX(1, m->m.height);
		m->backdrop_offset_x = MAX(-1.0, MIN(1.0, m->backdrop_offset_x * decay + dx));
		m->backdrop_offset_y = MAX(-1.0, MIN(1.0, m->backdrop_offset_y * decay + dy));
		if (fabs(m->backdrop_offset_x) < 0.001)
			m->backdrop_offset_x = 0;
		if (fabs(m->backdrop_offset_y) < 0.001)
			m->backdrop_offset_y = 0;
	}
	m->backdrop_camera_x = m->canvas_x;
	m->backdrop_camera_y = m->canvas_y;
	m->backdrop_last = now;
	return !navigating && (m->backdrop_offset_x != 0 || m->backdrop_offset_y != 0);
}

/* A pan is starting: settle any zoom or glide so they don't fight it */
static void
pan_begin(Monitor *m)
{
	if (m->zoom_start) {
		m->zoom_start = 0;
		zoom_apply(m, m->zoom_to);
	}
	m->pan_glide = 0;
	m->pan_vx = m->pan_vy = 0;
	m->pan_px = m->canvas_x;
	m->pan_py = m->canvas_y;
	m->pan_last = anim_now();
}

/* Sample pan velocity so a release can keep the canvas gliding */
static void
pan_track(Monitor *m)
{
	uint32_t now = anim_now(), dt = now - m->pan_last;

	if (dt < 8)
		return; /* too short an interval for a stable sample */
	if (dt > 100) {
		m->pan_vx = m->pan_vy = 0; /* the drag paused */
	} else {
		m->pan_vx = 0.5 * m->pan_vx + 0.5 * (m->canvas_x - m->pan_px) / dt;
		m->pan_vy = 0.5 * m->pan_vy + 0.5 * (m->canvas_y - m->pan_py) / dt;
	}
	m->pan_px = m->canvas_x;
	m->pan_py = m->canvas_y;
	m->pan_last = now;
}

static void
pan_release(Monitor *m)
{
	if (m->backdrop_offset_x != 0 || m->backdrop_offset_y != 0) {
		/* A held, stationary drag may not have rendered for some time. */
		m->backdrop_last = anim_now();
		anim_schedule();
	}
	if ((uint32_t)(anim_now() - m->pan_last) > 50
			|| hypot(m->pan_vx, m->pan_vy) < ANIM_GLIDE_MIN || !anim_start())
		return;
	m->pan_glide = 1;
	m->pan_last = anim_now();
}

/* Glide with exponentially decaying velocity */
static int
pan_glide_tick(Monitor *m, uint32_t now)
{
	double dt = (double)(uint32_t)(now - m->pan_last), decay;

	if (!anim_enabled() || !(m->canvas_tags & m->tagset[m->seltags]))
		return m->pan_glide = 0;
	if (dt <= 0)
		return 1;
	decay = exp(-dt / ANIM_GLIDE_MS);
	m->canvas_x += m->pan_vx * ANIM_GLIDE_MS * (1.0 - decay);
	m->canvas_y += m->pan_vy * ANIM_GLIDE_MS * (1.0 - decay);
	m->pan_vx *= decay;
	m->pan_vy *= decay;
	m->pan_last = now;
	if (hypot(m->pan_vx, m->pan_vy) < ANIM_GLIDE_MIN)
		m->pan_glide = 0;
	canvas_layout(m);
	return m->pan_glide;
}

/* Snapshots are only pictures; the pointer passes through them */
static bool
snapshot_accepts_input(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	return false;
}

static void
snapshot_copy(Snapshot *s, struct wlr_scene_tree *src, int ox, int oy)
{
	struct wlr_scene_node *node;
	SnapshotPart part;

	wl_list_for_each(node, &src->children, link) {
		int x = ox + node->x, y = oy + node->y;
		SnapshotPart *slot;

		if (!node->enabled)
			continue;
		memset(&part, 0, sizeof(part));
		part.x = x;
		part.y = y;
		if (node->type == WLR_SCENE_NODE_TREE) {
			snapshot_copy(s, wlr_scene_tree_from_node(node), x, y);
			continue;
		} else if (node->type == WLR_SCENE_NODE_RECT) {
			struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node), *copy;
			if (!(copy = wlr_scene_rect_create(s->tree, rect->width, rect->height, rect->color)))
				continue;
			copy->accepts_input = false;
			part.node = &copy->node;
			part.width = rect->width;
			part.height = rect->height;
			memcpy(part.color, rect->color, sizeof(part.color));
		} else if (node->type == WLR_SCENE_NODE_BUFFER) {
			struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node), *copy;
			int bw, bh;
			if (!buffer->buffer || !(copy = wlr_scene_buffer_create(s->tree, buffer->buffer)))
				continue;
			bw = buffer->buffer->width;
			bh = buffer->buffer->height;
			if (buffer->transform & WL_OUTPUT_TRANSFORM_90) {
				bw = buffer->buffer->height;
				bh = buffer->buffer->width;
			}
			wlr_scene_buffer_set_source_box(copy, &buffer->src_box);
			wlr_scene_buffer_set_transform(copy, buffer->transform);
			wlr_scene_buffer_set_filter_mode(copy, WLR_SCALE_FILTER_BILINEAR);
			copy->point_accepts_input = snapshot_accepts_input;
			part.node = &copy->node;
			part.width = buffer->dst_width > 0 ? buffer->dst_width : bw;
			part.height = buffer->dst_height > 0 ? buffer->dst_height : bh;
			part.opacity = buffer->opacity;
			part.src_box = buffer->src_box;
			/* Preserve the displayed frame immediately, before the first tick. */
			wlr_scene_buffer_set_dest_size(copy, part.width, part.height);
			wlr_scene_buffer_set_opacity(copy, part.opacity);
		} else {
			continue;
		}
		wlr_scene_node_set_position(part.node, x, y);
		if (!(slot = wl_array_add(&s->parts, sizeof(*slot)))) {
			wlr_scene_node_destroy(part.node);
			continue;
		}
		*slot = part;
	}
}

static Snapshot *
snapshot_create(Client *c)
{
	Snapshot *s;
	SnapshotPart *p;
	int w = 0, h = 0;

	if (!c->scene->node.enabled || c->leaving || !anim_enabled())
		return NULL;
	s = ecalloc(1, sizeof(*s));
	if (!(s->tree = wlr_scene_tree_create(c->scene->node.parent))) {
		free(s);
		return NULL;
	}
	wl_array_init(&s->parts);
	wlr_scene_node_set_position(&s->tree->node, c->scene->node.x, c->scene->node.y);
	snapshot_copy(s, c->scene, 0, 0);
	wl_array_for_each(p, &s->parts) {
		w = MAX(w, p->x + p->width);
		h = MAX(h, p->y + p->height);
	}
	s->cx = w / 2.0;
	s->cy = h / 2.0;
	s->animation = ANIM_CLOSE;
	s->mon = c->mon;
	s->x = c->scene->node.x;
	double pop = c->pop_start ? ANIM_OPEN_SCALE + (1.0 - ANIM_OPEN_SCALE) * c->opacity : 1.0;
	s->frame = (struct wlr_box){s->x, c->scene->node.y,
			MAX(1, (int)round(c->rendered.width * pop)),
			MAX(1, (int)round(c->rendered.height * pop))};
	s->scale = c->visual_scale;
	s->opacity = c->opacity;
	s->start = anim_start();
	wl_list_insert(&snapshots, &s->link);
	return s;
}

/* Keep c's last frame on screen to fade out after c unmaps. */
static void
anim_close(Client *c)
{
	snapshot_create(c);
}

/* Tags change immediately; their outgoing picture slides independently of
 * the client's next layout, canvas state, focus, and application redraws. */
static void
anim_send(Client *c, uint32_t newtags)
{
	Monitor *m = c->mon;
	Snapshot *s;
	anim_cancel_transfer(c);

	if (!VISIBLEON(c, m) || (newtags & m->tagset[m->seltags]))
		return;
	if (!(s = snapshot_create(c)))
		return;
	s->animation = ANIM_WORKSPACE;
	s->client = c;
	s->source_tags = c->tags & m->tagset[m->seltags];
	s->target_tags = newtags;
	s->slide_to = __builtin_ctz(newtags) > __builtin_ctz(s->source_tags)
			? m->m.width : -m->m.width;
	snapshot_render(s, s->start);
}

static void
snapshot_destroy(Snapshot *s)
{
	wl_list_remove(&s->link);
	wlr_scene_node_destroy(&s->tree->node);
	wl_array_release(&s->parts);
	free(s);
}

static void
anim_cancel_transfer(Client *c)
{
	Snapshot *s, *tmp;
	wl_list_for_each_safe(s, tmp, &snapshots, link)
		if (s->client == c)
			snapshot_destroy(s);
}

/* Complete the picture-to-client handoff after the destination layout has
 * placed the live window. Both pictures occupy the same pixels at this point. */
static void
anim_transfer_arrivals(Monitor *m)
{
	Snapshot *s, *tmp;
	uint32_t now = anim_now();

	wl_list_for_each_safe(s, tmp, &snapshots, link) {
		Client *c = s->client;
		if (s->mon != m || !c || !VISIBLEON(c, m))
			continue;
		if (anim_progress(ANIM_WORKSPACE, s->start, now) < 1.0 && anim_enabled()) {
			double zoom = canvas_active(c) ? m->zoom : 1.0;
			struct wlr_box base = c->rendered;
			uint32_t start = anim_start();
			c->geom_from = client_displayed_geom(c, now);
			c->geom_from.width = MAX(1, (int)round(s->frame.width / zoom));
			c->geom_from.height = MAX(1, (int)round(s->frame.height / zoom));
			c->geom_scale_from = s->scale / zoom;
			c->geom_start = start;
			c->slide_from = s->tree->node.x - base.x;
			c->slide_to = 0;
			c->slide_from_y = s->tree->node.y - base.y;
			c->slide_opacity_from = s->opacity;
			c->slide_start = start;
			c->pop_start = 0;
			c->leaving = 0;
			client_render(c);
		}
		snapshot_destroy(s);
	}
}

/* Returns 1 once s has finished */
static int
snapshot_render(Snapshot *s, uint32_t now)
{
	int sending = s->animation == ANIM_WORKSPACE;
	double t = anim_progress(s->animation, s->start, now);
	double k = sending ? 1.0 : 1.0 - (1.0 - ANIM_CLOSE_SCALE) * t;
	float a = sending ? 1.0f : (float)(1.0 - t), color[4];
	SnapshotPart *p;

	/* A transfer belongs to its source view. Drop the picture when that view
	 * is left, or its destination becomes visible, to avoid duplicate windows. */
	if (sending && (!(s->mon->tagset[s->mon->seltags] & s->source_tags)
			|| (s->mon->tagset[s->mon->seltags] & s->target_tags)))
		return 1;
	if (t >= 1.0)
		return 1;
	if (sending) {
		/* Focusing a remaining tile can raise it while it fills the gap. */
		wlr_scene_node_raise_to_top(&s->tree->node);
		wlr_scene_node_set_position(&s->tree->node,
				s->x + (int)round(s->slide_to * t), s->tree->node.y);
	}
	wl_array_for_each(p, &s->parts) {
		int w = MAX(0, (int)round(p->width * k)), h = MAX(0, (int)round(p->height * k));
		wlr_scene_node_set_enabled(p->node, true);
		wlr_scene_node_set_position(p->node, (int)round(s->cx + (p->x - s->cx) * k),
				(int)round(s->cy + (p->y - s->cy) * k));
		if (p->node->type == WLR_SCENE_NODE_RECT) {
			for (int i = 0; i < 4; i++)
				color[i] = p->color[i] * a;
			wlr_scene_rect_set_size(wlr_scene_rect_from_node(p->node), w, h);
			wlr_scene_rect_set_color(wlr_scene_rect_from_node(p->node), color);
		} else {
			wlr_scene_buffer_set_source_box(wlr_scene_buffer_from_node(p->node), &p->src_box);
			wlr_scene_buffer_set_dest_size(wlr_scene_buffer_from_node(p->node), w, h);
			wlr_scene_buffer_set_opacity(wlr_scene_buffer_from_node(p->node), p->opacity * a);
		}
		if (sending)
			scene_clip_node(p->node, p->src_box, (struct wlr_box){
					s->tree->node.x + p->node->x, s->tree->node.y + p->node->y, w, h},
					&s->mon->m);
	}
	return 0;
}

/* Advance every animation by one frame; returns whether any is still running */
static int
anim_tick(void)
{
	uint32_t now = anim_now();
	Snapshot *s, *tmp;
	Monitor *m;
	Client *c;
	int active = 0;
	double t;

	wl_list_for_each(m, &mons, link) {
		if (m->backdrop_start) {
			t = anim_progress(ANIM_ZOOM, m->backdrop_start, now);
			m->backdrop_mix = m->backdrop_from + (m->backdrop_to - m->backdrop_from) * t;
			if (t >= 1.0)
				m->backdrop_start = 0;
			active |= m->backdrop_start != 0;
			backdrop_update(m);
		}
		if (m->zoom_start) {
			t = anim_progress(ANIM_ZOOM, m->zoom_start, now);
			if (t >= 1.0)
				m->zoom_start = 0;
			zoom_apply(m, m->zoom_from + (m->zoom_to - m->zoom_from) * t);
			active |= m->zoom_start != 0;
		}
		if (m->pan_glide)
			active |= pan_glide_tick(m, now);
		if (m->backdrop_offset_x != 0 || m->backdrop_offset_y != 0) {
			active |= backdrop_motion(m, now);
			backdrop_update(m);
		}
	}
	wl_list_for_each(c, &clients, link) {
		if (!c->geom_start && !c->pop_start && !c->slide_start)
			continue;
		client_render(c);
		active |= c->geom_start || c->pop_start || c->slide_start;
	}
	wl_list_for_each_safe(s, tmp, &snapshots, link) {
		if (snapshot_render(s, now))
			snapshot_destroy(s);
		else
			active = 1;
	}
	return active;
}

static void
anim_cleanup(void)
{
	Snapshot *s, *tmp;
	wl_list_for_each_safe(s, tmp, &snapshots, link)
		snapshot_destroy(s);
}

static void
anim_cleanupmon(Monitor *m)
{
	Snapshot *s, *tmp;
	wl_list_for_each_safe(s, tmp, &snapshots, link)
		if (s->mon == m)
			snapshot_destroy(s);
}

#define CANVAS_GAP 100

static void dwindle(Monitor *m);

static const Layout layouts[] = {
    {"><>", NULL},
    {"[T]", dwindle},
};

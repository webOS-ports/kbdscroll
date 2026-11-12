/*
 * Host tests for kbdscroll's handling of touchscreens with one contact stream
 * (protocol A, or a single slot). The daemon is one file; it is included here with its main() renamed
 * so the static functions can be driven directly. What it writes to the screen
 * is captured through a pipe.
 */
#define main kbdscroll_main
#include "../kbdscroll.c"
#undef main

#include <assert.h>

static int failures, pipefd[2];

#define CHECK(cond) do { if (!(cond)) { \
	fprintf(stderr, "%s:%d: %s: FAILED: %s\n", __FILE__, __LINE__, __func__, #cond); \
	failures++; } } while (0)

struct cap { struct input_event ev[64]; int n; };

static void reset(int typea, int shared)
{
	if (pipefd[0] > 0) { close(pipefd[0]); close(pipefd[1]); }
	if (pipe2(pipefd, O_NONBLOCK) < 0) { perror("pipe2"); exit(2); }
	scr_fd = pipefd[1];
	scr_w = scr_h = 721;		/* mtk-tpd */
	scr_typea = typea;
	scr_shared = shared;
	real_down = 0;
	inj_down = 0;
	inj_slot = 0;
}

static struct cap drain(void)
{
	struct cap c;
	memset(&c, 0, sizeof c);
	while (c.n < 64 && read(pipefd[0], &c.ev[c.n], sizeof c.ev[0]) == (ssize_t)sizeof c.ev[0])
		c.n++;
	return c;
}

static int count(const struct cap *c, int type, int code, int value, int any_value)
{
	int n = 0;
	for (int i = 0; i < c->n; i++)
		if (c->ev[i].type == type && c->ev[i].code == code &&
		    (any_value || c->ev[i].value == value))
			n++;
	return n;
}

static void feed(int type, int code, int value)
{
	struct input_event e;
	memset(&e, 0, sizeof e);
	e.type = type; e.code = code; e.value = value;
	scr_event(&e);
}

/* mtk-tpd's real frame, as captured on the Titan Pocket. */
static void real_frame_a(int x, int y)
{
	feed(EV_ABS, ABS_MT_TOUCH_MAJOR, 23);
	feed(EV_ABS, ABS_MT_TRACKING_ID, 0);
	feed(EV_ABS, ABS_MT_POSITION_X, x);
	feed(EV_ABS, ABS_MT_POSITION_Y, y);
	feed(EV_SYN, SYN_MT_REPORT, 0);
	feed(EV_SYN, SYN_REPORT, 0);
}

static void real_lift_a(void)
{
	feed(EV_KEY, BTN_TOUCH, 0);
	feed(EV_SYN, SYN_MT_REPORT, 0);
	feed(EV_SYN, SYN_REPORT, 0);
}

/* Replay what kbdscroll wrote back into scr_event(), as the kernel would. */
static void echo(const struct cap *c)
{
	for (int i = 0; i < c->n; i++)
		scr_event(&c->ev[i]);
}

static void test_typea_down_frame(void)
{
	reset(1, 1);
	inj_touch(100, 200);
	struct cap c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_SLOT, 0, 1) == 0);
	CHECK(count(&c, EV_ABS, ABS_MT_TRACKING_ID, INJ_ID, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_X, 100, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 200, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_X, 0, 1) == 1);	/* no step-off */
	CHECK(count(&c, EV_SYN, SYN_MT_REPORT, 0, 0) == 1);
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 1, 0) == 1);
	CHECK(c.n > 0 && c.ev[c.n - 1].type == EV_SYN && c.ev[c.n - 1].code == SYN_REPORT);
	/* The contact report comes before the frame closes. */
	int mt = -1, rep = -1;
	for (int i = 0; i < c.n; i++) {
		if (c.ev[i].type == EV_SYN && c.ev[i].code == SYN_MT_REPORT) mt = i;
		if (c.ev[i].type == EV_SYN && c.ev[i].code == SYN_REPORT) rep = i;
	}
	CHECK(mt >= 0 && mt < rep);
	CHECK(inj_down == 1);
}

static void test_typea_move_frame(void)
{
	reset(1, 1);
	inj_touch(100, 200);
	drain();
	inj_touch(100, 260);
	struct cap c = drain();
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 0, 1) == 0);
	CHECK(count(&c, EV_ABS, ABS_MT_TRACKING_ID, INJ_ID, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 260, 0) == 1);
	CHECK(count(&c, EV_SYN, SYN_MT_REPORT, 0, 0) == 1);
}

static void test_typea_lift_frame(void)
{
	reset(1, 1);
	inj_touch(100, 200);
	inj_touch(100, 300);	/* a real drag: no tap guard */
	drain();
	inj_up();
	struct cap c = drain();
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 0, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_TRACKING_ID, 0, 1) == 0);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_X, 0, 1) == 0);
	CHECK(count(&c, EV_SYN, SYN_MT_REPORT, 0, 0) == 1);
	CHECK(count(&c, EV_SYN, SYN_REPORT, 0, 0) == 1);
	CHECK(inj_down == 0);
}

/* A protocol B screen with slots to spare keeps the old frames exactly. */
static void test_typeb_unchanged(void)
{
	reset(0, 0);
	inj_slot = 9;
	real_down = 1;		/* ignored: not a shared stream */
	inj_touch(100, 200);
	struct cap c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_SLOT, 9, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_X, 101, 0) == 1);	/* step-off */
	CHECK(count(&c, EV_SYN, SYN_MT_REPORT, 0, 1) == 0);
	inj_up();
	c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_TRACKING_ID, -1, 0) == 1);
	CHECK(count(&c, EV_SYN, SYN_MT_REPORT, 0, 1) == 0);
}

/* A contact let go where it came down would be a tap: it is carried on in
 * its own direction first, by at least the guard, then lifted. */
static void test_no_tap(void)
{
	reset(1, 1);
	int g = tap_guard();
	CHECK(g == 30);

	/* Did not move at all: carried down. */
	inj_touch(360, 360);
	drain();
	inj_up();
	struct cap c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 360 + g, 0) == 1);
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 0, 0) == 1);
	/* the move comes before the lift */
	int mv = -1, up = -1;
	for (int i = 0; i < c.n; i++) {
		if (c.ev[i].type == EV_ABS && c.ev[i].code == ABS_MT_POSITION_Y) mv = i;
		if (c.ev[i].type == EV_KEY && c.ev[i].code == BTN_TOUCH) up = i;
	}
	CHECK(mv >= 0 && mv < up);

	/* Moved a little up and left, mostly left: carried left. */
	inj_touch(360, 360);
	inj_touch(350, 357);
	drain();
	inj_up();
	c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_X, 360 - g, 0) == 1);

	/* Moved a little up: carried up. */
	inj_touch(360, 360);
	inj_touch(360, 351);
	drain();
	inj_up();
	c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 360 - g, 0) == 1);

	/* At the bottom edge: carried back inside the screen. */
	inj_touch(360, 710);
	drain();
	inj_up();
	c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 710 - g, 0) == 1);

	/* Exactly the guard already: no extra move. */
	inj_touch(360, 360);
	inj_touch(360, 360 + g);
	drain();
	inj_up();
	c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 0, 1) == 0);
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 0, 0) == 1);
}

/* A long vertical slide: near the screen edge the contact lifts, and the next
 * one waits until the finger has gone a slop further, then comes down already
 * moved by it. A finger that stops at the edge leaves no tap behind. */
static void test_regrab_waits(void)
{
	reset(1, 1);
	o_scale = 2.0;
	o_slop = 36;
	drag_y_start(300);
	struct cap c = drain();
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 1, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 360, 0) == 1);

	drag_y(240);			/* 360 - 120 */
	c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 240, 0) == 1);
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 0, 1) == 0);

	drag_y(160);			/* 360 - 280: inside the edge band */
	c = drain();
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 0, 0) == 1);
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 1, 0) == 0);
	CHECK(!inj_down);

	drag_y(160 - 35);		/* less than the slop on: nothing */
	c = drain();
	CHECK(c.n == 0);

	inj_up();			/* the finger lifts here: still nothing */
	c = drain();
	CHECK(c.n == 0);

	drag_y(160 - 40);		/* past the slop: down mid-screen, moved */
	c = drain();
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 1, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 360, 0) == 1);
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 280, 0) == 1);
	CHECK(inj_down);

	drag_y(160 - 50);		/* and follows on from there */
	c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 260, 0) == 1);
	CHECK(count(&c, EV_KEY, BTN_TOUCH, 1, 1) == 0);

	/* Downwards too. */
	reset(1, 1);
	drag_y_start(100);
	drag_y(240);			/* 360 + 280 */
	drain();
	CHECK(!inj_down);
	drag_y(240 + 36);
	c = drain();
	CHECK(count(&c, EV_ABS, ABS_MT_POSITION_Y, 360 + 72, 0) == 1);
	CHECK(inj_down);
}

/* A finger that came down on a shared stream without our noticing first:
 * the lift is not written, and neither is the guard's move. */
static void test_no_lift_over_a_finger(void)
{
	reset(1, 1);
	inj_touch(100, 200);
	drain();
	real_frame_a(470, 469);
	inj_up();
	struct cap c = drain();
	CHECK(c.n == 0);
	CHECK(inj_down == 0);
}

static void test_no_injection_over_a_finger(void)
{
	reset(1, 1);
	real_frame_a(470, 469);
	CHECK(real_down == 1);
	inj_touch(100, 200);
	struct cap c = drain();
	CHECK(c.n == 0);
	CHECK(inj_down == 0);
}

static void test_abandon_writes_nothing(void)
{
	reset(1, 1);
	inj_touch(100, 200);
	drain();
	real_frame_a(470, 469);
	inj_abandon();
	inj_up();		/* would lift the finger */
	struct cap c = drain();
	CHECK(c.n == 0);
	CHECK(inj_down == 0);
}

static void test_scr_event_typea(void)
{
	reset(1, 1);
	real_frame_a(470, 469);
	CHECK(real_down == 1);
	real_lift_a();
	CHECK(real_down == 0);
	/* Our own frames come back through the same node. */
	inj_touch(100, 200);
	struct cap c = drain();
	echo(&c);
	CHECK(real_down == 0);
	inj_up();
	c = drain();
	echo(&c);
	CHECK(real_down == 0);
	/* A contact without a tracking id is still a contact. */
	feed(EV_ABS, ABS_MT_POSITION_X, 5);
	feed(EV_ABS, ABS_MT_POSITION_Y, 5);
	feed(EV_SYN, SYN_MT_REPORT, 0);
	feed(EV_SYN, SYN_REPORT, 0);
	CHECK(real_down == 1);
	real_lift_a();
	CHECK(real_down == 0);
}

static void test_scr_event_one_slot(void)
{
	reset(0, 1);
	inj_slot = 0;
	feed(EV_ABS, ABS_MT_SLOT, 0);
	feed(EV_ABS, ABS_MT_TRACKING_ID, 17);
	CHECK(real_down == 1);
	feed(EV_ABS, ABS_MT_TRACKING_ID, -1);
	CHECK(real_down == 0);
	feed(EV_ABS, ABS_MT_TRACKING_ID, INJ_ID);
	CHECK(real_down == 0);
	/* Another slot is not ours to worry about. */
	feed(EV_ABS, ABS_MT_SLOT, 1);
	feed(EV_ABS, ABS_MT_TRACKING_ID, 3);
	CHECK(real_down == 0);
	feed(EV_ABS, ABS_MT_SLOT, 0);
}

/* ---- a protocol A surface (the Titan's mtk-pad) ---- */

static int pid[SLOTS], prx[SLOTS], pry[SLOTS], pfresh[SLOTS];

static void pad_reset(void)
{
	for (int i = 0; i < SLOTS; i++) {
		pid[i] = -1; prx[i] = pry[i] = 0; pfresh[i] = 0;
		pa_cx[i] = pa_cy[i] = PA_NONE;
	}
	pa_n = pa_pos = 0;
	pa_tid = -1;
}

static void pfeed(int type, int code, int value)
{
	struct input_event e;
	memset(&e, 0, sizeof e);
	e.type = type; e.code = code; e.value = value;
	pad_typea_event(&e, pid, prx, pry, pfresh, 0, 0);
}

/* One contact as mtk-pad reports it, captured on the Titan (12 Nov 2026). */
static void mtk_contact(int tid, int x, int y)
{
	pfeed(EV_ABS, ABS_MT_TRACKING_ID, tid);
	pfeed(EV_ABS, ABS_MT_POSITION_X, x);
	pfeed(EV_ABS, ABS_MT_POSITION_Y, y);
	pfeed(EV_ABS, ABS_MT_TOUCH_MAJOR, 6);
	pfeed(EV_ABS, ABS_MT_TOUCH_MINOR, 5);
	pfeed(EV_SYN, SYN_MT_REPORT, 0);
}

static void mtk_frame(int x, int y)
{
	mtk_contact(0, x, y);
	pfeed(EV_SYN, SYN_REPORT, 0);
}

static void mtk_lift(void)
{
	pfeed(EV_KEY, BTN_TOUCH, 0);
	pfeed(EV_SYN, SYN_MT_REPORT, 0);
	pfeed(EV_SYN, SYN_REPORT, 0);
}

/* What the main loop does with a fresh contact at SYN_REPORT. */
static void consume_fresh(void)
{
	for (int i = 0; i < SLOTS; i++)
		pfresh[i] = 0;
}

/* The bug: mtk-pad never sends a tracking id of -1, so the first contact
 * never came up and the next touch was not fresh. */
static void test_pad_typea_lift(void)
{
	pad_reset();
	pfeed(EV_ABS, ABS_MT_TRACKING_ID, 0);
	pfeed(EV_KEY, BTN_TOUCH, 1);
	pfeed(EV_ABS, ABS_MT_POSITION_X, 322);
	pfeed(EV_ABS, ABS_MT_POSITION_Y, 261);
	pfeed(EV_SYN, SYN_MT_REPORT, 0);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == 0);
	CHECK(pfresh[0] == 1);
	CHECK(prx[0] == 322 && pry[0] == 261);
	CHECK(pid[1] == -1);
	consume_fresh();

	mtk_frame(349, 265);
	CHECK(pid[0] == 0);
	CHECK(pfresh[0] == 0);			/* the same contact moving */
	CHECK(prx[0] == 349 && pry[0] == 265);

	mtk_lift();
	CHECK(pid[0] == -1);

	mtk_frame(790, 347);			/* tracking id 0 again */
	CHECK(pid[0] == 0);
	CHECK(pfresh[0] == 1);			/* a new touch, where it lands */
	CHECK(prx[0] == 790 && pry[0] == 347);
}

static void test_pad_typea_two_contacts(void)
{
	pad_reset();
	mtk_contact(0, 100, 200);
	mtk_contact(1, 900, 400);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == 0 && pid[1] == 1);
	CHECK(prx[0] == 100 && prx[1] == 900);
	CHECK(pry[0] == 200 && pry[1] == 400);
	CHECK(pid[2] == -1);
	consume_fresh();

	mtk_frame(110, 210);			/* the second one lifted */
	CHECK(pid[0] == 0 && pfresh[0] == 0);
	CHECK(pid[1] == -1);
}

/* Another contact in the same place in the list, without a frame between. */
static void test_pad_typea_new_id(void)
{
	pad_reset();
	mtk_frame(100, 200);
	consume_fresh();
	mtk_contact(5, 600, 300);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == 5);
	CHECK(pfresh[0] == 1);
}

/* No tracking ids at all, and a contact the frame closed without its
 * SYN_MT_REPORT: still a contact. An empty frame is still a lift. */
static void test_pad_typea_sparse(void)
{
	pad_reset();
	pfeed(EV_ABS, ABS_MT_POSITION_X, 40);
	pfeed(EV_ABS, ABS_MT_POSITION_Y, 50);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == 0 && pfresh[0] == 1);
	CHECK(prx[0] == 40 && pry[0] == 50);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == -1);
}

/* Positions are relative to the axis minimum, as for the other surfaces. */
static void test_pad_typea_min(void)
{
	struct input_event e;
	pad_reset();
	memset(&e, 0, sizeof e);
	e.type = EV_ABS; e.code = ABS_MT_POSITION_X; e.value = 150;
	pad_typea_event(&e, pid, prx, pry, pfresh, 100, 20);
	e.code = ABS_MT_POSITION_Y; e.value = 70;
	pad_typea_event(&e, pid, prx, pry, pfresh, 100, 20);
	e.type = EV_SYN; e.code = SYN_REPORT; e.value = 0;
	pad_typea_event(&e, pid, prx, pry, pfresh, 100, 20);
	CHECK(prx[0] == 50 && pry[0] == 50);
}

/* More contacts than slots: the extra ones are dropped, nothing overruns. */
static void test_pad_typea_overflow(void)
{
	pad_reset();
	for (int i = 0; i < SLOTS + 3; i++)
		mtk_contact(i, 10 * i, 5 * i);
	pfeed(EV_SYN, SYN_REPORT, 0);
	for (int i = 0; i < SLOTS; i++)
		CHECK(pid[i] == i && prx[i] == 10 * i);
	mtk_lift();
	for (int i = 0; i < SLOTS; i++)
		CHECK(pid[i] == -1);
}

/* A contact keeps its slot when one listed before it lifts: the Titan lists
 * the sliding finger first and a resting thumb after it. */
static void test_pad_typea_keeps_slot(void)
{
	pad_reset();
	mtk_contact(0, 400, 500);
	mtk_contact(1, 1294, 189);
	pfeed(EV_SYN, SYN_REPORT, 0);
	consume_fresh();

	mtk_contact(1, 1163, 217);		/* the finger lifted */
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == -1);
	CHECK(pid[1] == 1 && pfresh[1] == 0);	/* still the same contact */
	CHECK(prx[1] == 1163 && pry[1] == 217);

	mtk_contact(0, 717, 347);		/* the finger again, listed first */
	mtk_contact(1, 1313, 218);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == 0 && pfresh[0] == 1);
	CHECK(prx[0] == 717 && pry[0] == 347);
	CHECK(pid[1] == 1 && pfresh[1] == 0);
	CHECK(prx[1] == 1313);
}

/* Two contacts swapping places in the list stay in their slots. */
static void test_pad_typea_reordered(void)
{
	pad_reset();
	mtk_contact(3, 10, 20);
	mtk_contact(4, 30, 40);
	pfeed(EV_SYN, SYN_REPORT, 0);
	consume_fresh();
	mtk_contact(4, 31, 41);
	mtk_contact(3, 11, 21);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == 3 && prx[0] == 11 && pfresh[0] == 0);
	CHECK(pid[1] == 4 && prx[1] == 31 && pfresh[1] == 0);
}

/* An axis a contact leaves out of a frame keeps its value. */
static void test_pad_typea_missing_axis(void)
{
	pad_reset();
	mtk_frame(100, 200);
	consume_fresh();
	pfeed(EV_ABS, ABS_MT_TRACKING_ID, 0);
	pfeed(EV_ABS, ABS_MT_POSITION_X, 150);
	pfeed(EV_SYN, SYN_MT_REPORT, 0);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[0] == 0 && pfresh[0] == 0);
	CHECK(prx[0] == 150 && pry[0] == 200);
	pfeed(EV_ABS, ABS_MT_TRACKING_ID, 0);
	pfeed(EV_ABS, ABS_MT_POSITION_Y, 260);
	pfeed(EV_SYN, SYN_MT_REPORT, 0);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(prx[0] == 150 && pry[0] == 260);
}

/* ... also when it is listed in another place: what the contact before it
 * in the list sent last frame is not its value. */
static void test_pad_typea_missing_axis_reordered(void)
{
	pad_reset();
	mtk_contact(3, 10, 20);
	mtk_contact(4, 30, 40);
	pfeed(EV_SYN, SYN_REPORT, 0);
	consume_fresh();
	pfeed(EV_ABS, ABS_MT_TRACKING_ID, 4);
	pfeed(EV_ABS, ABS_MT_POSITION_X, 35);
	pfeed(EV_SYN, SYN_MT_REPORT, 0);
	pfeed(EV_SYN, SYN_REPORT, 0);
	CHECK(pid[1] == 4 && prx[1] == 35 && pry[1] == 40);
}

/* ---- resting contacts ---- */

static int lid[SLOTS], llanded[SLOTS], lwas[SLOTS], lx[SLOTS], ly[SLOTS];
static int lsx[SLOTS], lsy[SLOTS], lfar[SLOTS], lrest[SLOTS];
static long long ldown[SLOTS];
static int lkeys;	/* a key is held */

static void live_reset(void)
{
	o_slop = 72;
	o_rest_ms = 400;
	lkeys = 0;
	for (int i = 0; i < SLOTS; i++) {
		lid[i] = -1;
		llanded[i] = lwas[i] = lfar[i] = lrest[i] = 0;
		lx[i] = ly[i] = lsx[i] = lsy[i] = 0;
		ldown[i] = 0;
	}
}

/* One frame: slot i down at (x, y), landing now if it was up. */
static void ldown_at(int i, int x, int y, long long now)
{
	if (lid[i] < 0) {
		lid[i] = i;
		llanded[i] = 1;
		lsx[i] = x; lsy[i] = y;
		ldown[i] = now;
	}
	lx[i] = x; ly[i] = y;
}

static int lframe(int driver, long long now, int *first)
{
	int n = live_contacts(lid, llanded, lwas, lx, ly, lsx, lsy, ldown,
			      lfar, lrest, driver, lkeys, now, first);
	for (int i = 0; i < SLOTS; i++)
		llanded[i] = 0;
	return n;
}

/* A thumb resting when a finger lands rests; the finger is the live one. */
static void test_rest_when_another_lands(void)
{
	int first;
	live_reset();
	ldown_at(1, 1294, 189, 0);
	CHECK(lframe(-1, 0, &first) == 1 && first == 1);
	ldown_at(1, 1294, 189, 100);
	ldown_at(0, 700, 400, 100);
	CHECK(lframe(-1, 100, &first) == 1 && first == 0);
	CHECK(lrest[1] == 1);
	/* and it stays resting, wherever it is moved to */
	ldown_at(0, 600, 400, 110);
	ldown_at(1, 1100, 240, 110);
	CHECK(lframe(0, 110, &first) == 1 && first == 0);
}

/* The contact driving a slide is not made to rest by a landing: the new
 * one is a second finger, and two live contacts cancel the gesture. */
static void test_driver_does_not_rest(void)
{
	int first;
	live_reset();
	ldown_at(0, 700, 400, 0);
	lframe(-1, 0, &first);
	ldown_at(0, 600, 400, 30);
	lframe(-1, 30, &first);
	ldown_at(0, 550, 400, 40);
	ldown_at(1, 200, 300, 40);
	CHECK(lframe(0, 40, &first) == 2 && first == 0);
	CHECK(lrest[0] == 0 && lrest[1] == 0);
}

/* What stays when a contact lifts rests: the thumb, once the finger is up. */
static void test_rest_when_another_lifts(void)
{
	int first;
	live_reset();
	ldown_at(0, 700, 400, 0);
	ldown_at(1, 1294, 189, 0);
	CHECK(lframe(-1, 0, &first) == 2);	/* landed together: both live */
	lid[0] = -1;
	ldown_at(1, 1163, 217, 50);
	CHECK(lframe(-1, 50, &first) == 0 && first == -1);
	CHECK(lrest[1] == 1);
}

/* Still for o_rest_ms after landing: resting. Moved a slop before then:
 * not, however long it is down. 0 turns the time rule off. */
static void test_rest_by_time(void)
{
	int first;
	live_reset();
	ldown_at(0, 500, 500, 0);
	lframe(-1, 0, &first);
	ldown_at(0, 530, 500, 399);
	CHECK(lframe(-1, 399, &first) == 1);
	ldown_at(0, 530, 500, 400);
	CHECK(lframe(-1, 400, &first) == 0);
	ldown_at(0, 900, 500, 450);		/* moving now does not undo it */
	CHECK(lframe(-1, 450, &first) == 0);

	live_reset();
	ldown_at(0, 500, 500, 0);
	lframe(-1, 0, &first);
	ldown_at(0, 572, 500, 100);		/* exactly a slop */
	lframe(-1, 100, &first);
	ldown_at(0, 572, 500, 5000);
	CHECK(lframe(-1, 5000, &first) == 1 && first == 0);

	live_reset();
	o_rest_ms = 0;
	ldown_at(0, 500, 500, 0);
	lframe(-1, 0, &first);
	ldown_at(0, 500, 500, 100000);
	CHECK(lframe(-1, 100000, &first) == 1);
}

/* A resting slot that lifts and takes a new contact is live again. */
static void test_rest_cleared_by_landing(void)
{
	int first;
	live_reset();
	ldown_at(0, 500, 500, 0);
	lframe(-1, 0, &first);
	ldown_at(0, 500, 500, 500);
	CHECK(lframe(-1, 500, &first) == 0);
	lid[0] = -1;
	CHECK(lframe(-1, 510, &first) == 0);
	ldown_at(0, 300, 300, 520);
	CHECK(lframe(-1, 520, &first) == 1 && first == 0);
	CHECK(lrest[0] == 0);
}

/* While a key is held every contact rests, the one driving a slide too,
 * and those that land meanwhile; they stay resting once it is let go. A
 * contact that lands after the release is live. */
static void test_rest_while_a_key_is_held(void)
{
	int first;
	live_reset();
	ldown_at(0, 700, 400, 0);
	lframe(-1, 0, &first);
	ldown_at(0, 600, 400, 30);
	CHECK(lframe(-1, 30, &first) == 1);
	lkeys = 1;
	ldown_at(0, 500, 400, 40);
	CHECK(lframe(0, 40, &first) == 0 && first == -1);
	CHECK(lrest[0] == 1);
	ldown_at(1, 1, 450, 50);		/* a phantom while backspace is held */
	CHECK(lframe(-1, 50, &first) == 0);
	CHECK(lrest[1] == 1);
	lkeys = 0;
	ldown_at(0, 300, 400, 60);
	ldown_at(1, 1, 1, 60);
	CHECK(lframe(-1, 60, &first) == 0);
	lid[0] = lid[1] = -1;
	lframe(-1, 70, &first);
	ldown_at(0, 700, 300, 80);
	CHECK(lframe(-1, 80, &first) == 1 && first == 0);
}

/* Recorded on the Titan (12 Nov 2026), 0.72 s to 1.30 s of a capture: a
 * finger (id 0) slides while a thumb (id 1) rests on the top row; the finger
 * lifts, and on its own the thumb is reported moving by up to 280 units;
 * then the finger lands again. Before, the thumb took the finger's slot,
 * counted as a new touch and its jump as a slide. */
static const struct { int ms, n; struct { int id, x, y; } c[2]; } rec[] = {
	{  721, 2, { { 0, 225, 519 }, { 1, 1294, 189 } } },
	{  759, 2, { { 0, 234, 489 }, { 1, 1294, 189 } } },
	{  778, 2, { { 0, 267, 461 }, { 1, 1294, 189 } } },
	{  797, 2, { { 0, 311, 431 }, { 1, 1294, 189 } } },
	{  807, 2, { { 0, 337, 416 }, { 1, 1294, 189 } } },
	{  817, 2, { { 0, 382, 401 }, { 1, 1294, 189 } } },
	{  826, 2, { { 0, 427, 393 }, { 1, 1294, 189 } } },
	{  836, 2, { { 0, 476, 385 }, { 1, 1294, 189 } } },
	{  846, 2, { { 0, 534, 376 }, { 1, 1294, 189 } } },
	{  855, 2, { { 0, 603, 369 }, { 1, 1294, 189 } } },
	{  874, 1, { { 1, 1294, 189 } } },
	{  884, 1, { { 1, 1163, 217 } } },
	{  893, 1, { { 1, 1138, 222 } } },
	{  941, 1, { { 1, 1165, 227 } } },
	{  951, 1, { { 1, 1215, 226 } } },
	{  960, 1, { { 1, 1280, 223 } } },
	{  970, 1, { { 1, 1326, 221 } } },
	{  990, 1, { { 1, 1365, 217 } } },
	{ 1018, 1, { { 1, 1390, 214 } } },
	{ 1066, 1, { { 1, 1357, 215 } } },
	{ 1076, 1, { { 1, 1331, 215 } } },
	{ 1085, 1, { { 1, 1295, 216 } } },
	{ 1095, 1, { { 1, 1236, 217 } } },
	{ 1105, 1, { { 1, 1203, 218 } } },
	{ 1114, 1, { { 1, 1178, 219 } } },
	{ 1133, 1, { { 1, 1137, 221 } } },
	{ 1153, 1, { { 1, 1109, 222 } } },
	{ 1172, 1, { { 1, 1243, 228 } } },
	{ 1181, 1, { { 1, 1278, 228 } } },
	{ 1201, 2, { { 0, 717, 347 }, { 1, 1313, 218 } } },
	{ 1230, 2, { { 0, 669, 341 }, { 1, 1336, 206 } } },
	{ 1239, 2, { { 0, 626, 337 }, { 1, 1341, 203 } } },
	{ 1249, 2, { { 0, 580, 332 }, { 1, 1345, 201 } } },
	{ 1259, 2, { { 0, 533, 329 }, { 1, 1348, 199 } } },
	{ 1269, 2, { { 0, 495, 329 }, { 1, 1351, 198 } } },
	{ 1278, 2, { { 0, 460, 323 }, { 1, 1353, 197 } } },
	{ 1287, 2, { { 0, 416, 323 }, { 1, 1355, 196 } } },
	{ 1297, 2, { { 0, 370, 324 }, { 1, 1357, 195 } } },
};

static void test_recorded_resting_thumb(void)
{
	int first, live = 0, finger_live_at = -1;
	pad_reset();
	live_reset();
	for (size_t f = 0; f < sizeof rec / sizeof *rec; f++) {
		for (int c = 0; c < rec[f].n; c++)
			mtk_contact(rec[f].c[c].id, rec[f].c[c].x, rec[f].c[c].y);
		if (!rec[f].n)
			mtk_lift();
		else
			pfeed(EV_SYN, SYN_REPORT, 0);
		for (int i = 0; i < SLOTS; i++) {
			lid[i] = pid[i];
			if (pid[i] < 0)
				continue;
			if (pfresh[i]) {
				llanded[i] = 1;
				lsx[i] = prx[i]; lsy[i] = pry[i];
				ldown[i] = rec[f].ms;
			}
			lx[i] = prx[i]; ly[i] = pry[i];
		}
		consume_fresh();
		live = lframe(-1, rec[f].ms, &first);
		if (rec[f].n == 1 && rec[f].c[0].id == 1) {
			CHECK(pid[1] == 1);	/* the thumb kept its slot */
			CHECK(live == 0);	/* and is resting */
		}
		if (live && finger_live_at < 0 && rec[f].ms > 1000)
			finger_live_at = rec[f].ms;
	}
	CHECK(finger_live_at == 1201);		/* the finger, as it lands */
	CHECK(live == 1 && first == 0);
	CHECK(lrest[1] == 1);
}

int main(void)
{
	test_pad_typea_lift();
	test_pad_typea_two_contacts();
	test_pad_typea_new_id();
	test_pad_typea_sparse();
	test_pad_typea_min();
	test_pad_typea_overflow();
	test_pad_typea_keeps_slot();
	test_pad_typea_reordered();
	test_pad_typea_missing_axis();
	test_pad_typea_missing_axis_reordered();
	test_rest_when_another_lands();
	test_driver_does_not_rest();
	test_rest_when_another_lifts();
	test_rest_by_time();
	test_rest_cleared_by_landing();
	test_rest_while_a_key_is_held();
	test_recorded_resting_thumb();
	test_typea_down_frame();
	test_typea_move_frame();
	test_typea_lift_frame();
	test_typeb_unchanged();
	test_no_tap();
	test_regrab_waits();
	test_no_lift_over_a_finger();
	test_no_injection_over_a_finger();
	test_abandon_writes_nothing();
	test_scr_event_typea();
	test_scr_event_one_slot();
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("kbdscroll tests: all passed\n");
	return 0;
}

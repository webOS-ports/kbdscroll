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
		pid[i] = -1; prx[i] = pry[i] = 0; pfresh[i] = 0; pa_seen[i] = 0;
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

int main(void)
{
	test_pad_typea_lift();
	test_pad_typea_two_contacts();
	test_pad_typea_new_id();
	test_pad_typea_sparse();
	test_pad_typea_min();
	test_pad_typea_overflow();
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

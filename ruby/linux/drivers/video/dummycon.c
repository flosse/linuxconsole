/*
 *  linux/drivers/video/dummycon.c -- A dummy console driver
 *
 *  To be used if there's no other console driver (e.g. for plain VGA text)
 *  available, usually until fbcon takes console over.
 */

#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/init.h>

/*
 *  Dummy console driver
 */

#if defined(__arm__)
#define DUMMY_COLUMNS	ORIG_VIDEO_COLS
#define DUMMY_ROWS	ORIG_VIDEO_LINES
#else
#define DUMMY_COLUMNS	80
#define DUMMY_ROWS	25
#endif

static const char *dummycon_startup(struct vt_struct *vt, int init)
{
    vt->default_mode.vc_can_do_color = 1;
    vt->default_mode.vc_cols = DUMMY_COLUMNS;
    vt->default_mode.vc_rows = DUMMY_ROWS;
    return "dummy device";
}

static void dummycon_init(struct vc_data *vc)
{
    vc = vc->display_fg->default_mode.vc_can_do_color = 1;
    vc->vc_cols = vc->display_fg->default_mode.vc_cols;
    vc->vc_rows = vc->display_fg->default_mode.vc_rows;
}

static int dummycon_dummy(void)
{
    return 0;
}

#define DUMMY	(void *)dummycon_dummy

/*
 *  The console `switch' structure for the dummy console
 *
 *  Most of the operations are dummies.
 */

const struct consw dummy_con = {
    con_startup:	dummycon_startup,
    con_init:		dummycon_init,
    con_deinit:		DUMMY,
    con_clear:		DUMMY,
    con_putc:		DUMMY,
    con_putcs:		DUMMY,
    con_cursor:		DUMMY,
    con_scroll:		DUMMY,
    con_bmove:		DUMMY,
    con_switch:		DUMMY,
    con_blank:		DUMMY,
    con_font_op:	DUMMY,
    con_resize:		DUMMY,
    con_set_palette:	DUMMY,
    con_scrolldelta:	DUMMY,
};

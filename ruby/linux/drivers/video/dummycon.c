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
#elif defined(__hppa__)
#define DUMMY_COLUMNS	80	/* fixme ! (mine uses 160x64 at 1280x1024) */
#define DUMMY_ROWS	25
#else
#define DUMMY_COLUMNS	80
#define DUMMY_ROWS	25
#endif

#define MAX_DUMB_CONSOLES 4
static unsigned long dumb_num __initdata = 0;	/* disabled by default */
MODULE_PARM(dumb_num, "n");

static const char *dummycon_startup(struct vt_struct *vt, int init)
{
	struct vc_data *vc;

	vc = (struct vc_data *) kmalloc(sizeof(struct vc_data),
					GFP_KERNEL);
	vt->default_mode = vc;
	vc->display_fg = vt;
	vt->default_mode->vc_can_do_color = 0;
	vt->default_mode->vc_cols = DUMMY_COLUMNS;
	vt->default_mode->vc_rows = DUMMY_ROWS;
	return "dummy device";
}

static void dummycon_init(struct vc_data *vc)
{
	vc->vc_can_do_color =
	    vc->display_fg->default_mode->vc_can_do_color = 1;
	vc->vc_cols = vc->display_fg->default_mode->vc_cols;
	vc->vc_rows = vc->display_fg->default_mode->vc_rows;
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
	con_startup:		dummycon_startup,
	con_init:		dummycon_init,
	con_deinit:		DUMMY,
	con_clear:		DUMMY,
	con_putc:		DUMMY,
	con_putcs:		DUMMY,
	con_cursor:		DUMMY,
	con_scroll_region:	DUMMY,
	con_bmove:		DUMMY,
	con_resize:		DUMMY,
	con_blank:		DUMMY,
	con_font_op:		DUMMY,
	con_set_palette:	DUMMY,
	con_scroll:		DUMMY,
};

int dumb_init(void)
{
	struct vt_struct *vt =
	    (struct vt_struct *) kmalloc(sizeof(struct vt_struct),
					 GFP_KERNEL);
	const char *display_desc = NULL;

	if (!vt)
		return 1;
	memset(vt, 0, sizeof(struct vt_struct));
	vt->kmalloced = 1;
	vt->vt_sw = &dummy_con;
	display_desc = vt_map_display(vt, 1);
	if (!display_desc)
		return -ENODEV;
	printk("Console: %s %s %dx%d\n",
	       vt->default_mode->vc_can_do_color ? "colour" : "mono",
	       display_desc, vt->default_mode->vc_cols,
	       vt->default_mode->vc_rows);
	return 0;
}

int __init dumb_console_init(void)
{
	unsigned long i;
	for (i = 0; i < dumb_num && i < MAX_DUMB_CONSOLES; i++) {
		if (dumb_init())
			return 1;
	}
	return 0;
}

int __init dumbcon_setup(char *options)
{
	if (!options || !*options)
		return 0;
	dumb_num = simple_strtoul(options, 0, 0);
	return 0;
}

__setup("dumbcon=", dumbcon_setup);

#ifdef MODULE
void __exit dumb_module_exit(void)
{
	/* release_vt(&vga_vt); */
}

module_init(dumb_console_init);
module_exit(dumb_module_exit);

MODULE_LICENSE("GPL");
#endif

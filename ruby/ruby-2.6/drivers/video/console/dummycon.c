/*
 *  linux/drivers/video/dummycon.c -- A dummy console driver
 *
 *  To be used if there's no other console driver (e.g. for plain VGA text)
 *  available, usually until fbcon takes console over.
 */

#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/init.h>

/*
 *  Dummy console driver
 */

#if defined(__arm__)
#define DUMMY_COLUMNS	ORIG_VIDEO_COLS
#define DUMMY_ROWS	ORIG_VIDEO_LINES
#elif defined(__hppa__)
/* set by Kconfig. Use 80x25 for 640x480 and 160x64 for 1280x1024 */
#include <linux/config.h>
#define DUMMY_COLUMNS	CONFIG_DUMMY_CONSOLE_COLUMNS
#define DUMMY_ROWS	CONFIG_DUMMY_CONSOLE_ROWS
#else
#define DUMMY_COLUMNS	80
#define DUMMY_ROWS	25
#endif
#define MAX_DUMB_CONSOLES 16

static unsigned long dumb_num = 0;
static unsigned long dumb_vc_count = 0;
static struct vt_struct dummy_vt;
static struct vc_data default_mode;

static const char *dummycon_startup(struct vt_struct *vt, int init)
{
	vt->default_mode = &default_mode;
	vt->default_mode->vc_cols = DUMMY_COLUMNS;
	vt->default_mode->vc_rows = DUMMY_ROWS;
	return "dummy device";
}

static void dummycon_init(struct vc_data *vc, int init)
{
    vc->vc_can_do_color = 1;
    if (init) {
	vc->vc_cols = DUMMY_COLUMNS;
	vc->vc_rows = DUMMY_ROWS;
    } else
	vc_resize(vc, DUMMY_COLUMNS, DUMMY_ROWS);
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
    .con_startup 	= dummycon_startup,
    .con_init 		= dummycon_init,
    .con_deinit 	= DUMMY,
    .con_clear 		= DUMMY,
    .con_putc 		= DUMMY,
    .con_putcs 		= DUMMY,
    .con_cursor		= DUMMY,
    .con_scroll_region 	= DUMMY,
    .con_bmove	 	= DUMMY,
    .con_switch 	= DUMMY,
    .con_blank 		= DUMMY,
    .con_font_op 	= DUMMY,
    .con_set_palette 	= DUMMY,
    .con_scroll 	= DUMMY,
};

int __init dumbcon_init(void)
{
	const char *display_desc = NULL;

	memset(&dummy_vt, 0, sizeof(struct vt_struct));
	dummy_vt.vt_kmalloced = 0;
	dummy_vt.vt_sw = &dummy_con;		
	display_desc = vt_map_display(&dummy_vt, 1, MAX_NR_USER_CONSOLES);
	if (!display_desc) return -ENODEV;
	printk("Console: mono %s %dx%d vc:%d-%d\n", display_desc,
		dummy_vt.default_mode->vc_cols,	
		dummy_vt.default_mode->vc_rows,
		dummy_vt.first_vc + 1, dummy_vt.first_vc + dummy_vt.vc_count);
	return 0;
}

int dumbcon_add(void)
{
        const char *display_desc = NULL;
        struct vt_struct *vt;

        vt = (struct vt_struct *) kmalloc(sizeof(struct vt_struct),GFP_KERNEL);

        if (!vt) return -ENOMEM;

        memset(vt, 0, sizeof(struct vt_struct));
        vt->vt_kmalloced = 1;
        vt->vt_sw = &dummy_con;
        display_desc = vt_map_display(vt, 1, MIN_NR_CONSOLES);
        if (!display_desc) {
		kfree(vt);
	        return -ENODEV;
	}
	printk("Console: mono %s %dx%d vc:%d-%d\n", display_desc,
		vt->default_mode->vc_cols,	
		vt->default_mode->vc_rows,
		vt->first_vc + 1, vt->first_vc + vt->vc_count);
        return 0;
}

int __init dumb_console_init(void)
{
        unsigned long i;
        for(i=0; i<dumb_num && i<MAX_DUMB_CONSOLES; i++ ) {
	        if(dumbcon_add()) return 1;
        }
        return 0;
}

int __init dumbcon_setup(char *options)
{
        if (!options || !*options)
	        return 0;
        dumb_num =  simple_strtoul(options, 0, 0);
        return 0;
}

__setup("dumbcon=", dumbcon_setup);

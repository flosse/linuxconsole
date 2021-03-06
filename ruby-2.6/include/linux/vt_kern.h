#ifndef _VT_KERN_H
#define _VT_KERN_H

/*
 * All the data structs defining the VT tty system.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/vt.h>
#include <linux/kbd_kern.h>
#include <linux/device.h>

#define MIN_NR_CONSOLES 1	/* must be at least 1 */
#define MAX_NR_CONSOLES 63	/* serial lines start at 64 */
#define MAX_NR_USER_CONSOLES 16 /* number of VCs per VT */

/* scroll */
#define SM_UP       (1)
#define SM_DOWN     (2)

/* cursor */
#define CM_DRAW     (1)
#define CM_ERASE    (2)
#define CM_CHANGE   (3)
#define CM_MOVE     (4)

#define CUR_DEF         0
#define CUR_NONE        1
#define CUR_UNDERLINE   2
#define CUR_LOWER_THIRD 3
#define CUR_LOWER_HALF  4
#define CUR_TWO_THIRDS  5
#define CUR_BLOCK       6
#define CUR_HWMASK      0x0f
#define CUR_SWMASK      0xfff0

#define CUR_DEFAULT	CUR_UNDERLINE

/*
 *      Low-Level Functions
 */
#define IS_VISIBLE (vc == vc->display_fg->fg_console) 

/*
 * Presently, a lot of graphics programs do not restore the contents of
 * the higher font pages.  Defining this flag will avoid use of them, but
 * will lose support for PIO_FONTRESET.  Note that many font operations are
 * not likely to work with these programs anyway; they need to be
 * fixed.  The linux/Documentation directory includes a code snippet
 * to save and restore the text font.
 */
#ifdef CONFIG_VGA_CONSOLE
#define BROKEN_GRAPHICS_PROGRAMS 1
#endif

#define BUF_SIZE (CONFIG_BASE_SMALL ? 256 : PAGE_SIZE)

extern int is_console_locked(void);
extern unsigned char color_table[];
extern int default_red[];
extern int default_grn[];
extern int default_blu[];

/*
 * Data structure describing single virtual console
 *
 * Fields marked with [#] must be set by the low-level driver.
 * Fields marked with [!] can be changed by the low-level driver
 * to achieve effects such as fast scrolling by changing the origin.
 */
#define NPAR 16

struct vc_data {
	unsigned short vc_num;		/* Console number */
	unsigned int vc_cols;		/* [#] Console size */
	unsigned int vc_rows;
	unsigned int vc_size_row;	/* Bytes per row */
	unsigned int vc_scan_lines;	/* # of scan lines */
	unsigned int vc_screensize;	/* Size of screen */
	unsigned char vc_mode;		/* KD_TEXT, ... */
	unsigned long vc_origin;	/* [!] Start of real screen */
	unsigned long vc_scr_end;	/* [!] End of real screen */
	unsigned long vc_visible_origin;/* [!] Top of visible window */
	unsigned int vc_scrollback;	/* [!] Scrollback size */
	unsigned int vc_top, vc_bottom;	/* Scrolling region */
	unsigned short *vc_screenbuf;	/* In-memory character/attribute buffer */
	unsigned int vc_screenbuf_size;
	unsigned char vc_attr;		/* Current attributes */
	unsigned char vc_def_color;	/* Default colors */
	unsigned char vc_color;		/* Foreground & background */
	unsigned char vc_s_color;	/* Saved foreground & background */
	unsigned char vc_ulcolor;	/* Color for underline mode */
	unsigned char vc_halfcolor;	/* Color for half intensity mode */
	/* cursor */
	unsigned int vc_cursor_type;
	unsigned short vc_complement_mask;	/* [#] Xor mask for mouse pointer */
	unsigned short vc_s_complement_mask;	/* Saved mouse pointer mask */
	unsigned short vc_video_erase_char;	/* Background erase character */
	unsigned int vc_x, vc_y;	/* Cursor position */
	unsigned long vc_pos;		/* Cursor address */
	unsigned int vc_saved_x;
	unsigned int vc_saved_y;
	unsigned int vc_state;		/* Escape sequence parser state */
	unsigned int vc_npar, vc_par[NPAR];	/* Parameters of current escape sequence */
	struct kbd_struct kbd_table;	/* VC keyboard state */
	unsigned short vc_hi_font_mask;	/* [#] Attribute set for upper 256 chars of font or 0 if not supported */
	struct console_font vc_font;	/* VC current font set */
	struct vt_struct *display_fg;	/* Ptr to display */
	struct tty_struct *vc_tty;	/* TTY we are attached to */
	/* data for manual vt switching */
	struct vt_mode vt_mode;
	int vt_pid;
	int vt_newvt;
	/* mode flags */
	unsigned int vc_charset:1;	/* Character set G0 / G1 */
	unsigned int vc_s_charset:1;	/* Saved character set */
	unsigned int vc_disp_ctrl:1;	/* Display chars < 32? */
	unsigned int vc_toggle_meta:1;	/* Toggle high bit? */
	unsigned int vc_decscnm:1;	/* Screen Mode */
	unsigned int vc_decom:1;	/* Origin Mode */
	unsigned int vc_decawm:1;	/* Autowrap Mode */
	unsigned int vc_dectcem:1;	/* Text Cursor Enable */
	unsigned int vc_irm:1;		/* Insert/Replace Mode */
	unsigned int vc_deccolm:1;	/* 80/132 Column Mode */
	/* attribute flags */
	unsigned int vc_intensity:2;	/* 0=half-bright, 1=normal, 2=bold */
	unsigned int vc_underline:1;
	unsigned int vc_blink:1;
	unsigned int vc_reverse:1;
	unsigned int vc_s_intensity:2;	/* saved rendition */
	unsigned int vc_s_underline:1;
	unsigned int vc_s_blink:1;
	unsigned int vc_s_reverse:1;
	/* misc */
	unsigned int vc_priv1:1;	/* indicating a private control
					   function */
	unsigned int vc_priv2:1;	/* indicating a private control
					   function */
	unsigned int vc_priv3:1;	/* indicating a private control
					   function */
	unsigned int vc_priv4:1;	/* indicating a private control
					   function (used to be called "ques") */
	unsigned int vc_need_wrap:1;
	unsigned int vc_can_do_color:1;
	unsigned int vc_report_mouse:2;
        unsigned int vc_kmalloced:1;
	unsigned char vc_utf:1;		/* Unicode UTF-8 encoding */
	unsigned char vc_utf_count;
	int vc_utf_char;
	unsigned int vc_tab_stop[8];	/* Tab stops. 256 columns. */
	unsigned char vc_palette[16 * 3];	/* Colour palette for VGA+ */
	unsigned short *vc_translate;
	unsigned char vc_G0_charset;
	unsigned char vc_G1_charset;
	unsigned char vc_saved_G0;
	unsigned char vc_saved_G1;
	unsigned int vc_bell_pitch;	/* Console bell pitch */
	unsigned int vc_bell_duration;	/* Console bell duration */
	unsigned long vc_uni_pagedir;
	unsigned long *vc_uni_pagedir_loc;/* [!] Location of uni_pagedir 
						 variable for this console */
	wait_queue_head_t paste_wait;	/* For selections */
	/* Internal flags */
	unsigned int vc_decscl;		/* operating level */
	unsigned int vc_c8bit:1;	/* 8-bit controls */
	unsigned int vc_d8bit:1;	/* 8-bit data */
	unsigned int vc_shift:1;	/* single shift */
	/* Private modes */
	unsigned int vc_decckm:1;	/* Cursor Keys */
	unsigned int vc_decsclm:1;	/* Scrolling */
	unsigned int vc_decarm:1;	/* Autorepeat */
	unsigned int vc_decnrcm:1;	/* National Replacement Character Set */
	unsigned int vc_decnkm:1;	/* Numeric Keypad */
	/* ANSI / ISO mode flags */
	unsigned int vc_kam:1;		/* Keyboard Action */
	unsigned int vc_crm:1;		/* Console Representation */
	unsigned int vc_lnm:1;		/* Line feed/New line */
	/* Charset mappings */
	unsigned char vc_GL_charset;
	unsigned char vc_GR_charset;
	unsigned char vc_G2_charset;
	unsigned char vc_G3_charset;
	unsigned char vc_GS_charset;
	unsigned char vc_saved_G2;
	unsigned char vc_saved_G3;
	unsigned char vc_saved_GS;
};

struct consw {
	struct module *owner;
	const char *(*con_startup)(struct vt_struct *, int);
	void	(*con_init)(struct vc_data *, int);
	void	(*con_deinit)(struct vc_data *);
	void	(*con_clear)(struct vc_data *, int, int, int, int);
	void	(*con_putc)(struct vc_data *, int, int, int);
	void	(*con_putcs)(struct vc_data *, const unsigned short *, int, int, int);
	void	(*con_cursor)(struct vc_data *, int);
	int	(*con_scroll_region)(struct vc_data *, int, int, int, int);
	void	(*con_bmove)(struct vc_data *, int, int, int, int, int, int);
	int	(*con_switch)(struct vc_data *);
	int	(*con_blank)(struct vc_data *, int, int);
	int	(*con_font_set)(struct vc_data *, struct console_font *, unsigned);
	int	(*con_font_get)(struct vc_data *, struct console_font *);
	int	(*con_font_default)(struct vc_data *, struct console_font *, char *);
	int	(*con_font_copy)(struct vc_data *, int);
	int	(*con_resize)(struct vc_data *, unsigned int, unsigned int);
	int	(*con_set_palette)(struct vc_data *, unsigned char *);
	int	(*con_scroll)(struct vc_data *, int);
	int	(*con_set_origin)(struct vc_data *);
	void	(*con_save_screen)(struct vc_data *);
	u8	(*con_build_attr)(struct vc_data *, u8, u8, u8, u8, u8);
	void	(*con_invert_region)(struct vc_data *, u16 *, int);
	u16*	(*con_screen_pos)(struct vc_data *, int);
	unsigned long (*con_getxy)(struct vc_data *, unsigned long, int *, int *);
};

struct vt_struct {
        unsigned short vt_num;          /* VT id */
	struct vc_data *fg_console;	/* VC being displayed */
	struct vc_data *last_console;	/* VC we last switched from */
	struct vc_data *want_vc;	/* VC we want to switch to */
	int scrollback_delta;
	int cursor_original;
	char kmalloced;		/* Did we use kmalloced ? */
	char vt_dont_switch;	/* VC switching flag */
	char vt_blanked;	/* Is this display blanked */
	int blank_mode;		/* 0:none 1:suspendV 2:suspendH 3:powerdown */
	int blank_interval;	/* How long before blanking */
	int off_interval;
	int blank_state;
	int blank_timer_expired;
	struct timer_list timer;	/* Timer for VT blanking */
	struct timer_list beep;	/* Timer for adjusting console beeping */
	struct pm_dev *pm_con;		/* power management */
	/* 
	 * This is a temporary buffer used to prepare a tty console write
         * so that we can easily avoid touching user space while holding the
         * console spinlock. It is shared by with vc_screen read/write tty 
	 * calls.
         */
	struct semaphore lock;		/* Lock for con_buf */
	char con_buf[BUF_SIZE];
	const struct consw *vt_sw;	/* Display driver for VT */
	struct vc_data *default_mode;	/* Default mode */
	struct work_struct vt_work;	/* VT work queue */
	struct input_handle *keyboard;  /* Keyboard attached */
	struct input_handle *beeper;	/* Bell noise support */
	void *data_hook;		/* Hook for driver data */	
	unsigned int first_vc;
	unsigned int vc_count;
	struct vc_data *vc_cons[MAX_NR_USER_CONSOLES];	/* VT's VC pool */
        struct list_head node;
        struct proc_dir_entry *procdir;
	unsigned char vt_ledstate;
	unsigned char vt_ledioctl;
	char *display_desc;
	struct	class_device	dev;		/* Generic device interface */
};

extern struct list_head vt_list;
extern struct vt_struct *admin_vt;

#define	to_vt_struct(n) container_of(n, struct vt_struct, dev)

/* universal VT emulation functions */
void vte_ris(struct vc_data *vc, int do_clear);
inline void vte_cr(struct vc_data *vc);
void vte_lf(struct vc_data *vc);
inline void vte_bs(struct vc_data *vc);
void vte_ed(struct vc_data *vc, int vpar);
void vte_decsc(struct vc_data *vc);
void terminal_emulation(struct tty_struct *tty, int c);

/* vt.c */
/* Some debug stub to catch some of the obvious races in the VT code */
#if 1
#define WARN_CONSOLE_UNLOCKED() WARN_ON(!is_console_locked() && !oops_in_progress)
#else
#define WARN_CONSOLE_UNLOCKED()
#endif

const char *vt_map_display(struct vt_struct *vt, int init, int vc_count);
void vt_map_input(struct vt_struct *vt);
struct vc_data *find_vc(int currcons);
struct vc_data *vc_allocate(unsigned int console);
inline void set_console(struct vc_data *vc);
int vc_resize(struct vc_data *vc, unsigned int lines, unsigned int cols);
int vc_disallocate(struct vc_data *vc);
void reset_vc(struct vc_data *vc);
void add_softcursor(struct vc_data *vc);
void set_cursor(struct vc_data *vc);
void hide_cursor(struct vc_data *vc);
void gotoxy(struct vc_data *vc, int new_x, int new_y);
inline void gotoxay(struct vc_data *vc, int new_x, int new_y);
void reset_palette(struct vc_data *vc);
void set_palette(struct vc_data *vc);
void scroll_up(struct vc_data *vc, int);
void scroll_down(struct vc_data *vc, int);
void scroll_region_up(struct vc_data *vc, unsigned int t, unsigned int b, int nr);
void scroll_region_down(struct vc_data *vc, unsigned int t, unsigned int b, int nr); 
void default_attr(struct vc_data *vc);
void update_attr(struct vc_data *vc);
void insert_char(struct vc_data *vc, unsigned int nr);
void delete_char(struct vc_data *vc, unsigned int nr);
void insert_line(struct vc_data *vc, unsigned int nr);
void delete_line(struct vc_data *vc, unsigned int nr);
void set_origin(struct vc_data *vc);
inline void clear_region(struct vc_data *vc, int x, int y, int width, int height);
void do_update_region(struct vc_data *vc, unsigned long start, int count);
void update_region(struct vc_data *vc, unsigned long start, int count);
void update_screen(struct vc_data *vc);
inline int resize_screen(struct vc_data *vc, int width, int height);
inline unsigned short *screenpos(struct vc_data *vc, int offset, int viewed);
inline void save_screen(struct vc_data *vc);
void do_blank_screen(struct vt_struct *vt, int gfx_mode);
void unblank_vt(struct vt_struct *vt);
void unblank_screen(void);
void poke_blanked_console(struct vt_struct *vt);
int con_font_op(struct vc_data *vc, struct console_font_op *op);
int con_font_set(struct vc_data *vc, struct console_font_op *op);
int con_font_get(struct vc_data *vc, struct console_font_op *op);
int con_font_default(struct vc_data *vc, struct console_font_op *op);
int con_font_copy(struct vc_data *vc, struct console_font_op *op);
int take_over_console(struct vt_struct *vt, const struct consw *sw);

int tioclinux(struct tty_struct *tty, unsigned long arg);
extern struct tty_driver *console_device(int *);

/* consolemap.c */
struct unimapinit;
struct unipair;

int con_set_trans_old(struct vc_data *vc, unsigned char __user *table);
int con_get_trans_old(struct vc_data *vc, unsigned char __user *table);
int con_set_trans_new(struct vc_data *vc, unsigned short __user *table);
int con_get_trans_new(struct vc_data *vc, unsigned short __user *table);
int con_clear_unimap(struct vc_data *vc, struct unimapinit *ui);
int con_set_unimap(struct vc_data *vc, ushort ct, struct unipair __user *list);
int con_get_unimap(struct vc_data *vc, ushort ct, ushort __user *uct,
		   struct unipair __user *list);
int con_set_default_unimap(struct vc_data *vc);
void con_free_unimap(struct vc_data *vc);
void con_protect_unimap(struct vc_data *vc, int rdonly);
int con_copy_unimap(struct vc_data *dst, struct vc_data *src);

/* vt_ioctl.c */
void complete_change_console(struct vc_data *new_vc, struct vc_data *old_vc);
void change_console(struct vc_data *new_vc, struct vc_data *old_vc);

/* vt_sysfs.c*/
int __init vt_create_sysfs_dev_files (struct vt_struct *vt);
void __init vt_sysfs_init(void);
/* vt_proc.c */
#ifdef CONFIG_PROC_FS
extern int vt_proc_attach(struct vt_struct *vt);
extern int vt_proc_detach(struct vt_struct *vt);
#endif

#endif				/* _VT_KERN_H */

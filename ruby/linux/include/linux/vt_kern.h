#ifndef _VT_KERN_H
#define _VT_KERN_H

/*
 * this really is an extension of the vc_cons structure in console.c, but
 * with information needed by the vt package
 */

#include <linux/config.h>
#include <linux/pm.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/kbd_kern.h>
#include <linux/input.h>

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

#define CUR_DEFAULT CUR_UNDERLINE

/*
 *      Low-Level Functions
 */

#define IS_VISIBLE (vc->vc_num == vc->display_fg->fg_console->vc_num)

extern unsigned char color_table[];
extern int default_red[];
extern int default_grn[];
extern int default_blu[];      

/*
 * Data structure describing single virtual console except for data
 * used by vt.c.
 *
 * Fields marked with [#] must be set by the low-level driver.
 * Fields marked with [!] can be changed by the low-level driver
 * to achieve effects such as fast scrolling by changing the origin.
 */           

#define NPAR 16

struct vc_data {
        unsigned short  vc_num;                 /* Console number */
        unsigned int    vc_cols;                /* [#] Console size */
        unsigned int    vc_rows;
        unsigned int    vc_size_row;            /* Bytes per row */
        unsigned int 	vc_scan_lines;		/* # of scan lines */
	unsigned int 	vc_screensize;		/* Size of screen */
	unsigned char	vc_mode;		/* KD_TEXT, ... */
	unsigned long   vc_origin;              /* [!] Start of real screen */
        unsigned long   vc_scr_end;             /* [!] End of real screen */
        unsigned long   vc_visible_origin;      /* [!] Top of visible window */
        unsigned int 	vc_scrollback;		/* [!] Scrollback size */
	unsigned int    vc_top, vc_bottom;      /* Scrolling region */
	unsigned short  *vc_screenbuf;          /* In-memory character/attribute buffer */
        unsigned int    vc_screenbuf_size;
        unsigned char   vc_attr;                /* Current attributes */
        unsigned char   vc_def_color;           /* Default colors */
        unsigned char   vc_color;               /* Foreground & background */
        unsigned char   vc_s_color;             /* Saved foreground & background */
        unsigned char   vc_ulcolor;             /* Color for underline mode */
        unsigned char   vc_halfcolor;           /* Color for half intensity mode */
        unsigned short  vc_complement_mask;     /* [#] Xor mask for mouse pointer */
        unsigned short  vc_s_complement_mask;   /* Saved mouse pointer mask */
        unsigned short  vc_video_erase_char;    /* Background erase character */
        unsigned int    vc_x, vc_y;             /* Cursor position */
        unsigned long   vc_pos;                 /* Cursor address */
        unsigned int    vc_saved_x;
        unsigned int    vc_saved_y;
        unsigned int    vc_state;               /* Escape sequence parser state */
        unsigned int    vc_npar,vc_par[NPAR];   /* Parameters of current escape sequence */
	struct kbd_struct kbd_table;		/* VC keyboard state */       
	unsigned short  vc_hi_font_mask;        /* [#] Attribute set for upper 256 chars of font or 0 if not supported */
	struct console_font_op vc_font;		/* VC current font set */
	struct tty_struct *vc_tty;              /* TTY we are attached to */
	/* data for manual vt switching */
	struct vt_mode  vt_mode;
        int             vt_pid;
        int             vt_newvt;
        /* mode flags */
        unsigned int    vc_charset      : 1;    /* Character set G0 / G1 */
        unsigned int    vc_s_charset    : 1;    /* Saved character set */
        unsigned int    vc_disp_ctrl    : 1;    /* Display chars < 32? */
        unsigned int    vc_toggle_meta  : 1;    /* Toggle high bit? */
        unsigned int    vc_decscnm      : 1;    /* Screen Mode */
        unsigned int    vc_decom        : 1;    /* Origin Mode */
        unsigned int    vc_decawm       : 1;    /* Autowrap Mode */
        unsigned int    vc_dectcem      : 1;    /* Text Cursor Enable */
        unsigned int    vc_irm          : 1;    /* Insert/Replace Mode */
        unsigned int    vc_deccolm      : 1;    /* 80/132 Column Mode */
        /* attribute flags */
        unsigned int    vc_intensity    : 2;    /* 0=half-bright, 1=normal, 2=bold */
        unsigned int    vc_underline    : 1;
        unsigned int    vc_blink        : 1;
        unsigned int    vc_reverse      : 1;
        unsigned int    vc_s_intensity  : 2;    /* saved rendition */
        unsigned int    vc_s_underline  : 1;
        unsigned int    vc_s_blink      : 1;
        unsigned int    vc_s_reverse    : 1;                        
        /* misc */
	unsigned int    vc_priv1        : 1;    /* indicating a private control
                                                        function */
        unsigned int    vc_priv2        : 1;    /* indicating a private control
                                                        function */
        unsigned int    vc_priv3        : 1;    /* indicating a private control
							function */
	unsigned int    vc_priv4        : 1;    /* indicating a private control
					function (used to be called "ques") */
        unsigned int    vc_need_wrap    : 1;
        unsigned int    vc_can_do_color : 1;
        unsigned int    vc_report_mouse : 2;
        unsigned char   vc_utf          : 1;    /* Unicode UTF-8 encoding */
        unsigned char   vc_utf_count;
                 int    vc_utf_char;
        unsigned int    vc_tab_stop[5];         /* Tab stops. 160 columns. */
        unsigned char   vc_palette[16*3];       /* Colour palette for VGA+ */
        unsigned int    vc_translate;		/* Current ACM */	
        unsigned char   vc_G0_charset;
        unsigned char   vc_G1_charset;
        unsigned char   vc_saved_G0;
        unsigned char   vc_saved_G1;
        unsigned int    vc_bell_pitch;          /* Console bell pitch */
        unsigned int    vc_bell_duration;       /* Console bell duration */
        unsigned int    vc_cursor_type;
        struct vt_struct *display_fg;		/* Ptr to display */
	unsigned long   vc_uni_pagedir;
        unsigned long   *vc_uni_pagedir_loc;  /* [!] Location of uni_pagedir var
iable for this console */
	wait_queue_head_t paste_wait;	        /* For selections */	
        /* Internal flags */
        unsigned int    vc_decscl;              /* operating level */
        unsigned int    vc_c8bit        : 1;    /* 8-bit controls */
        unsigned int    vc_d8bit        : 1;    /* 8-bit data */
        unsigned int    vc_shift        : 1;    /* single shift */
        /* Private modes */
        unsigned int    vc_decckm       : 1;    /* Cursor Keys */
        unsigned int    vc_decsclm      : 1;    /* Scrolling */
        unsigned int    vc_decarm       : 1;    /* Autorepeat */       
        unsigned int    vc_decnrcm      : 1;    /* National Replacement Characte
r Set */
        unsigned int    vc_decnkm       : 1;    /* Numeric Keypad */
        /* ANSI / ISO mode flags */
        unsigned int    vc_kam          : 1;    /* Keyboard Action */
        unsigned int    vc_crm          : 1;    /* Console Representation */
        unsigned int    vc_lnm          : 1;    /* Line feed/New line */
        /* Charset mappings */
        unsigned char   vc_GL_charset;
        unsigned char   vc_GR_charset;
        unsigned char   vc_G2_charset;
        unsigned char   vc_G3_charset;
        unsigned char   vc_GS_charset;
        unsigned char   vc_saved_G2;
        unsigned char   vc_saved_G3;
        unsigned char   vc_saved_GS;
};                                 

struct consw {
        const char *(*con_startup)(struct vt_struct *, int);
        void    (*con_init)(struct vc_data *);
        void    (*con_deinit)(struct vc_data *);
        void    (*con_clear)(struct vc_data *, int, int, int, int);
        void    (*con_putc)(struct vc_data *, int, int, int);
        void    (*con_putcs)(struct vc_data *, const unsigned short *, int, int, int);
        void    (*con_cursor)(struct vc_data *, int);
        int     (*con_scroll_region)(struct vc_data *, int, int, int, int);
        void    (*con_bmove)(struct vc_data *, int, int, int, int, int, int);
        int     (*con_blank)(struct vc_data *, int);
        int     (*con_font_op)(struct vc_data *, struct console_font_op *);
        int	(*con_resize)(struct vc_data *, unsigned int, unsigned int);
	int     (*con_set_palette)(struct vc_data *, unsigned char *);
        int     (*con_scroll)(struct vc_data *, int);
        int     (*con_set_origin)(struct vc_data *);
        u8      (*con_build_attr)(struct vc_data *, u8, u8, u8, u8, u8);
        void    (*con_invert_region)(struct vc_data *, u16 *, int);
};

extern const struct consw dummy_con;   	/* dummy console buffer */
extern const struct consw newport_con; 	/* SGI Newport console  */
extern const struct consw prom_con;    	/* SPARC PROM console */
extern const struct consw nvvga_con;	/* NVIDIA text console */ 

void take_over_console(struct vt_struct *vt, const struct consw *sw);

struct vt_struct {
	struct vc_data  *fg_console;		/* VC being displayed */
        struct vc_data 	*last_console;     	/* VC we last switched from */
	struct vc_data  *want_vc;		/* VC we want to switch to */
	int scrollback_delta;			
	int cursor_original;
	char kmalloced;
	char vt_dont_switch;			/* VC switching flag */
	char vt_blanked;             		/* Is this display blanked */
	int blank_mode;	       /* 0:none 1:suspendV 2:suspendH 3:powerdown */
	int blank_interval;			/* How long before blanking */
	int off_interval;			
	struct timer_list timer;                /* Timer for VT blanking */
	struct pm_dev *pm_con;			/* power management */
       /* This is a temporary buffer used to prepare a tty console write
	* so that we can easily avoid touching user space while holding the
 	* console spinlock. It is shared by with vc_screen read/write tty calls.
	*/
	char con_buf[PAGE_SIZE];		
	struct semaphore lock;  		/* Lock for con_buf 	 */
	void *data_hook;			/* Hook for driver data */
	const struct consw *vt_sw;		/* Display driver for VT */
	const struct consw *cache_sw;		/* Save consw when KD_GRAPHIC */
	struct vc_data *default_mode;	 	/* Default mode */
	struct tq_struct vt_tq;			/* VT task queue */
	struct input_handle *keyboard;		/* Keyboard attached */
	unsigned int first_vc;
        struct vc_data *vc_cons[MAX_NR_USER_CONSOLES];  /* VT's VC pool */
	struct vt_struct *next;				
}; 

extern struct vt_struct *vt_cons;
extern struct vt_struct *admin_vt;

extern inline void set_console(struct vc_data *vc)
{
        vc->display_fg->want_vc = vc;
        schedule_task(&vc->display_fg->vt_tq);
}

/* universal VT emulation functions */
void vte_ris(struct vc_data *vc, int do_clear);
inline void vte_cr(struct vc_data *vc);
void vte_lf(struct vc_data *vc);
inline void vte_bs(struct vc_data *vc);
void vte_ed(struct vc_data *vc, int vpar);
void vte_decsc(struct vc_data *vc);
void terminal_emulation(struct tty_struct *tty, int c);

/* vt.c */
struct console_font_op;
const char* create_vt(struct vt_struct *vt, int init);
int release_vt(struct vt_struct *vt);
struct vc_data* find_vc(int currcons);
struct vc_data* vc_allocate(unsigned int console);
int vc_resize(struct vc_data *vc, unsigned int lines, unsigned int cols);
int vc_disallocate(struct vc_data *vc);
void vc_init(struct vc_data *vc, int do_clear);
void add_softcursor(struct vc_data *vc);
void set_cursor(struct vc_data *vc);
void hide_cursor(struct vc_data *vc);
void update_cursor_attr(struct vc_data *vc);
void gotoxy(struct vc_data *vc, int new_x, int new_y);
inline void gotoxay(struct vc_data *vc, int new_x, int new_y);
void reset_palette(struct vc_data *vc);
void set_palette(struct vc_data *vc);
inline int resize_screen(struct vc_data *vc, int cols, int rows);
void scroll_up(struct vc_data *vc, int);
void scroll_down(struct vc_data *vc, int);
void scroll_region_up(struct vc_data *vc,unsigned int t,unsigned int b,int nr);
void scroll_region_down(struct vc_data *vc, unsigned int t, unsigned int b, int nr);
void default_attr(struct vc_data *vc);
void update_attr(struct vc_data *vc);
void insert_char(struct vc_data *vc, unsigned int nr);
void delete_char(struct vc_data *vc, unsigned int nr);
void insert_line(struct vc_data *vc, unsigned int nr);
void delete_line(struct vc_data *vc, unsigned int nr);
void set_origin(struct vc_data *vc);
inline void clear_region(struct vc_data *vc,int x,int y,int width,int height);
void do_update_region(struct vc_data *vc, unsigned long start, int count);
void update_region(struct vc_data *vc, unsigned long start, int count);
void update_screen(struct vc_data *vc);
inline unsigned short *screenpos(struct vc_data *vc, int offset, int viewed);
void invert_screen(struct vc_data *vc, int offset, int count, int viewed);
void kd_mksound(struct vc_data *vc, unsigned int hz, unsigned int ticks);
void unblank_screen(struct vt_struct *vt);
void poke_blanked_console(struct vt_struct *vt);
int pm_con_request(struct pm_dev *dev, pm_request_t rqst, void *data);

struct tty_struct;
void respond_string(const char * p, struct tty_struct * tty);	
int tioclinux(struct tty_struct *tty, unsigned long arg);

/* consolemap.c */

struct unimapinit;
struct unipair;

void console_map_init(void);
int con_set_trans_old(struct vc_data *vc, unsigned char * table);
int con_get_trans_old(struct vc_data *vc, unsigned char * table);
int con_set_trans_new(struct vc_data *vc, unsigned short * table);
int con_get_trans_new(struct vc_data *vc, unsigned short * table);
int con_clear_unimap(struct vc_data *vc, struct unimapinit *ui);
int con_set_unimap(struct vc_data *vc, ushort ct, struct unipair *list);
int con_get_unimap(struct vc_data *vc, ushort ct, ushort *uct, struct unipair *list);
int con_set_default_unimap(struct vc_data *vc);
void con_free_unimap(struct vc_data *vc);
void con_protect_unimap(struct vc_data *vc, int rdonly);
int con_copy_unimap(struct vc_data *dstcons, struct vc_data *srccons);

/* vt_ioctl.c */
void change_console(struct vc_data *new_vc, struct vc_data *old_vc);
void complete_change_console(struct vc_data *new_vc, struct vc_data *old_vc);
void reset_vc(struct vc_data *vc);

#endif /* _VT_KERN_H */

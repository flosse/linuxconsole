/*
 * selection.h
 *
 * Interface between console.c, tty_io.c, vt.c, vc_screen.c and selection.c
 */

#ifndef _LINUX_SELECTION_H_
#define _LINUX_SELECTION_H_

#include <linux/vt_buffer.h>

struct vc_data *vc;
extern int sel_cons;

extern void clear_selection(void);
extern int set_selection(const unsigned long arg, struct tty_struct *tty, int user);
extern int paste_selection(struct tty_struct *tty);
extern int sel_loadlut(const unsigned long arg);
extern int mouse_reporting(struct tty_struct *tty);
extern void mouse_report(struct tty_struct *tty, int butt, int mrx, int mry);

#define video_num_columns	(vc->vc_cols)
#define video_num_lines		(vc->vc_rows)
#define video_size_row		(vc->vc_size_row)
#define can_do_color		(vc->vc_can_do_color)

extern unsigned short *screen_pos(struct vc_data *vc, int w_offset, int viewed);
extern u16 screen_glyph(struct vc_data *vc, int offset);
extern void complement_pos(struct vc_data *vc, int offset);
extern void invert_screen(struct vc_data *vc, int offset, int count, int shift);

extern void getconsxy(struct vc_data *vc, char *p);
extern void putconsxy(struct vc_data *vc, char *p);

extern u16 vcs_scr_readw(struct vc_data *vc, const u16 *org);
extern void vcs_scr_writew(struct vc_data *vc, u16 val, u16 *org);

#endif

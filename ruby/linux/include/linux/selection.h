/*
 * selection.h
 *
 * Interface between console.c, tty_io.c, vt.c, vc_screen.c and selection.c
 */

#ifndef _LINUX_SELECTION_H_
#define _LINUX_SELECTION_H_

#include <linux/vt_buffer.h>

extern int sel_cons;

void clear_selection(void);
int set_selection(const unsigned long arg, struct tty_struct *tty, int user);
int paste_selection(struct tty_struct *tty);
int sel_loadlut(const unsigned long arg);
int mouse_reporting(struct tty_struct *tty);
void mouse_report(struct tty_struct *tty, int butt, int mrx, int mry);

unsigned short *screen_pos(struct vc_data *vc, int w_offset, int viewed);
u16 screen_glyph(struct vc_data *vc, int offset);
void complement_pos(struct vc_data *vc, int offset);
void invert_screen(struct vc_data *vc, int offset, int count, int shift);

void getconsxy(struct vc_data *vc, char *p);
void putconsxy(struct vc_data *vc, char *p);

u16 vcs_scr_readw(struct vc_data *vc, const u16 *org);
void vcs_scr_writew(struct vc_data *vc, u16 val, u16 *org);

#endif

/*
 * decvte.c - DEC VT terminal emulation code. 
 * Copyright (C) 2002  James Simmons (jsimmons@www.infradead.org)
 *
 * I moved all the VT emulation code out of console.c to here. It makes life
 * much easier and the code smaller. It also allows other devices to emulate
 * a TTY besides the video system. People can also change the makefile to
 * support a different emulation if they wanted. 
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/vt_kern.h>
#include <linux/vt_buffer.h>
#include <linux/selection.h>
#include <linux/consolemap.h>
#include <linux/config.h>
#include <linux/version.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

/*
 * DEC VT emulator
 */

/*  Different states of the emulator */
enum { ESinit,
	/* ESC substates */
	ESesc, ESacs, ESscf, ESgzd4, ESg1d4, ESg2d4,
	ESg3d4, ESg1d6, ESg2d6, ESg3d6, ESdocs,
	/* CSI substates */
	EScsi, EScsi_getpars, EScsi_gotpars, EScsi_space,
	EScsi_exclam, EScsi_dquote, EScsi_dollar, EScsi_and,
	EScsi_squote, EScsi_star, EScsi_plus,
	/* OSC substates */
	ESosc, ESpalette,
	/* Misc. states */
	ESfunckey, ESignore,
};

#define __VTE_CSI       (vc->vc_c8bit == 0 ? "\033[" : "\233")
#define __VTE_DCS       (vc->vc_c8bit == 0 ? "\033P" : "\220")
#define __VTE_ST        (vc->vc_c8bit == 0 ? "\033\\" : "\234")
#define __VTE_APC       (vc->vc_c8bit == 0 ? "\033_" : "\237")

/*
 * this is what the terminal answers to a ESC-Z or csi0c query.
 */
#define VT100ID "\033[?1;2c"
#define VT102ID "\033[?6c"

/*
 * Here is the default bell parameters: 750HZ, 1/8th of a second
 */
#define DEFAULT_BELL_PITCH 750
#define DEFAULT_BELL_DURATION      (HZ/8)

#define foreground	(vc->vc_color & 0x0f)
#define background	(vc->vc_color & 0xf0)

/*
 * LINE FEED (LF)
 */
void vte_lf(struct vc_data *vc)
{
	/* don't scroll if above bottom of scrolling region, or
	 * if below scrolling region
	 */
	if (vc->vc_y + 1 == vc->vc_bottom)
		scroll_region_up(vc, vc->vc_top, vc->vc_bottom, 1);
	else if (vc->vc_y < vc->vc_rows - 1) {
		vc->vc_y++;
		vc->vc_pos += vc->vc_size_row;
	}
	vc->vc_need_wrap = 0;
}

/*
 * REVERSE LINE FEED (RI)
 */
static void vte_ri(struct vc_data *vc)
{
	/* don't scroll if below top of scrolling region, or
	 * if above scrolling region
	 */
	if (vc->vc_y == vc->vc_top)
		scroll_region_down(vc, vc->vc_top, vc->vc_bottom, 1);
	else if (vc->vc_y > 0) {
		vc->vc_y--;
		vc->vc_pos -= vc->vc_size_row;
	}
	vc->vc_need_wrap = 0;
}

/*
 * CARRIAGE RETURN (CR)
 */
inline void vte_cr(struct vc_data *vc)
{
	vc->vc_pos -= vc->vc_x << 1;
	vc->vc_need_wrap = vc->vc_x = 0;
}

/*
 * BACK SPACE (BS)
 */
inline void vte_bs(struct vc_data *vc)
{
	if (vc->vc_x) {
		vc->vc_pos -= 2;
		vc->vc_x--;
		vc->vc_need_wrap = 0;
	}
}

/*
 * CURSOR LINE TABULATION (CVT)
 *
 * NOTE:
 * In accordance with our interpretation of VT as LF we will treat CVT as
 * (par[0] * LF).  Not very creative, but at least consequent.
 */
static void vte_cvt(struct vc_data *vc, int vpar)
{
	int i;

	for (i = 0; i < vpar; i++) {
		vte_lf(vc);
	}
}

/*
 * CURSOR BACKWARD TABULATION (CBT)
 */
static void vte_cbt(struct vc_data *vc, int vpar)
{
	int i;

	for (i = 0; i < vpar; i++) {
		vc->vc_pos -= (vc->vc_x << 1);
		while (vc->vc_x > 0) {
			vc->vc_x--;
			if (vc->vc_tab_stop[vc->vc_x >> 5] & (1 << (vc->vc_x & 31)))
				break;
		}
		vc->vc_pos += (vc->vc_x << 1);
	}
}

/*
 * CURSOR FORWARD TABULATION (CHT)
 */
static void vte_cht(struct vc_data *vc, int vpar)
{
	int i;

	for (i = 0; i < vpar; i++) {
		vc->vc_pos -= (vc->vc_x << 1);
		while (vc->vc_x < vc->vc_cols - 1) {
			vc->vc_x++;
			if (vc->vc_tab_stop[vc->vc_x >> 5] & (1 << (vc->vc_x & 31)))
				break;
		}
		vc->vc_pos += (vc->vc_x << 1);
	}
}

/*
 * ERASE IN PAGE (ED)
 */
void vte_ed(struct vc_data *vc, int vpar)
{
	unsigned short *start;
	unsigned int count;

	switch (vpar) {
	case 0:		/* erase from cursor to end of display */
		count = (vc->vc_scr_end - vc->vc_pos) >> 1;
		start = (unsigned short *) vc->vc_pos;
		/* do in two stages */
		clear_region(vc, vc->vc_x, vc->vc_y, vc->vc_cols - vc->vc_x, 1);
		clear_region(vc, 0, vc->vc_y + 1, vc->vc_cols,
				vc->vc_rows - vc->vc_y - 1);
		break;
	case 1:		/* erase from start to cursor */
		count = ((vc->vc_pos - vc->vc_origin) >> 1) + 1;
		start = (unsigned short *) vc->vc_origin;
		/* do in two stages */
		clear_region(vc, 0, 0, vc->vc_cols, vc->vc_y);
		clear_region(vc, 0, vc->vc_y, vc->vc_x + 1, 1);
		break;
	case 2:		/* erase whole display */
		count = vc->vc_cols * vc->vc_rows;
		start = (unsigned short *) vc->vc_origin;
		clear_region(vc, 0, 0, vc->vc_cols, vc->vc_rows);
		break;
	default:
		return;
	}
	scr_memsetw(start, vc->vc_video_erase_char, 2 * count);
	vc->vc_need_wrap = 0;
}

/*
 * ERASE IN LINE (EL)
 */
static void vte_el(struct vc_data *vc, int vpar)
{
	unsigned short *start;
	unsigned int count;

	switch (vpar) {
	case 0:		/* erase from cursor to end of line */
		count = vc->vc_cols - vc->vc_x;
		start = (unsigned short *) vc->vc_pos;
		clear_region(vc, vc->vc_x, vc->vc_y, vc->vc_cols - vc->vc_x, 1);
		break;
	case 1:		/* erase from start of line to cursor */
		start = (unsigned short *) (vc->vc_pos - (vc->vc_x << 1));
		count = vc->vc_x + 1;
		clear_region(vc, 0, vc->vc_y, vc->vc_x + 1, 1);
		break;
	case 2:		/* erase whole line */
		start = (unsigned short *) (vc->vc_pos - (vc->vc_x << 1));
		count = vc->vc_cols;
		clear_region(vc, 0, vc->vc_y, vc->vc_cols, 1);
		break;
	default:
		return;
	}
	scr_memsetw(start, vc->vc_video_erase_char, 2 * count);
	vc->vc_need_wrap = 0;
}

/*
 * Erase character (ECH)
 *
 * NOTE:  This function is not available in DEC VT1xx terminals.
 */
static void vte_ech(struct vc_data *vc, int vpar)
{
	int count;

	if (!vpar)
		vpar++;
	count = (vpar > vc->vc_cols - vc->vc_x) ? (vc->vc_cols - vc->vc_x) : vpar;
	scr_memsetw((unsigned short *) vc->vc_pos, vc->vc_video_erase_char, 2 * count);
	clear_region(vc, vc->vc_x, vc->vc_y, count, 1);
	vc->vc_need_wrap = 0;
}

/*
 * SELECT GRAPHIC RENDITION (SGR)
 *
 * NOTE: The DEC vt1xx series only implements attribute values 0,1,4,5 and 7.
 */
static void vte_sgr(struct vc_data *vc)
{
	int i;

	for (i = 0; i <= vc->vc_npar; i++)
		switch (vc->vc_par[i]) {
		case 0:	/* all attributes off */
			default_attr(vc);
			break;
		case 1:	/* bold or increased intensity */
			vc->vc_intensity = 2;
			break;
		case 2:	/* faint or decreased intensity */
			vc->vc_intensity = 0;
			break;
		case 4:	/* singly underlined. */
			vc->vc_underline = 1;
			break;
		case 5:	/* slowly blinking (< 2.5 Hz) */
		case 6:	/* rapidly blinking (>= 2.5 Hz) */
			vc->vc_blink = 1;
			break;
		case 7:	/* negative image */
			vc->vc_reverse = 1;
			break;
		case 10:	/*  primary (default) font
				 * ANSI X3.64-1979 (SCO-ish?)
				 * Select primary font, don't display
				 * control chars if defined, don't set
				 * bit 8 on output.
				 */
			set_translate(vc, vc->vc_charset == 0 ?
				      vc->vc_G0_charset : vc->vc_G1_charset);
			vc->vc_disp_ctrl = 0;
			vc->vc_toggle_meta = 0;
			break;
		case 11:	/* first alternative font
				 * ANSI X3.64-1979 (SCO-ish?)
				 * Select first alternate font, lets
				 * chars < 32 be displayed as ROM chars.
				 */
			set_translate(vc, IBMPC_MAP);
			vc->vc_disp_ctrl = 1;
			vc->vc_toggle_meta = 0;
			break;
		case 12:	/* second alternative font 
				 * ANSI X3.64-1979 (SCO-ish?)
				 * Select second alternate font, toggle
				 * high bit before displaying as ROM char.      
				 */
			set_translate(vc, IBMPC_MAP);
			vc->vc_disp_ctrl = 1;
			vc->vc_toggle_meta = 1;
			break;
		case 21:	/* normal intensity */
		case 22:	/* normal intensity */
			vc->vc_intensity = 1;
			break;
		case 24:	/* not underlined (neither singly nor doubly) */
			vc->vc_underline = 0;
			break;
		case 25:	/* steady (not blinking) */
			vc->vc_blink = 0;
			break;
		case 27:	/* positive image */
			vc->vc_reverse = 0;
			break;
		case 38:	/* 
				 * foreground color (ISO 8613-6/ITU T.416) 
				 * Enables underscore, white foreground
				 * with white underscore (Linux - use
				 * default foreground).
				 */
			vc->vc_color = (vc->vc_def_color & 0x0f) | background;
			vc->vc_underline = 1;
			break;
		case 39:	/*
				 * default display color 
				 * ANSI X3.64-1979 (SCO-ish?)
                                 * Disable underline option.
                                 * Reset colour to default? It did this
                                 * before...
				 */
			vc->vc_color = (vc->vc_def_color & 0x0f) | background;
			vc->vc_underline = 0;
			break;
		case 49:	/* default background color */
			vc->vc_color = (vc->vc_def_color & 0xf0) | foreground;
			break;
		default:
			if (vc->vc_par[i] >= 30 && vc->vc_par[i] <= 37)
				vc->vc_color = color_table[vc->vc_par[i] - 30]
				    | background;
			else if (vc->vc_par[i] >= 40 && vc->vc_par[i] <= 47)
				vc->vc_color = (color_table[vc->vc_par[i] - 40] << 4)
				    | foreground;
			break;
		}
	update_attr(vc);
}

/*
 * Fake a DEC DSR for non-implemented features
 */
static void vte_fake_dec_dsr(struct tty_struct *tty, char *reply)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	sprintf(buf, "%s?%sn", __VTE_CSI, reply);
	puts_queue(vc, buf);
}

/*
 * CURSOR POSITION REPORT (CPR)
 * DEC EXTENDED CURSOR POSITION REPORT (DECXCPR)
 */
static void vte_cpr(struct tty_struct *tty, int ext)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	if (ext) {
		/*
		 * NOTE:  Since we do not (yet?) implement any form of page
		 * memory, we will always return the cursor position in page 1.
		 */
		sprintf(buf, "%s?%d;%d;1R", __VTE_CSI,
			vc->vc_y + (vc->vc_decom ? vc->vc_top + 1 : 1), vc->vc_x + 1);
	} else {
		sprintf(buf, "%s%d;%dR", __VTE_CSI,
			vc->vc_y + (vc->vc_decom ? vc->vc_top + 1 : 1), vc->vc_x + 1);
	}
	puts_queue(vc, buf);
}

/*
 * DEVICE STATUS REPORT (DSR)
 */
static inline void vte_dsr(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	sprintf(buf, "%s0n", __VTE_CSI);
	puts_queue(vc, buf);
}

/*
 * ANSWERBACK MESSAGE
 */
static inline void vte_answerback(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	
	puts_queue(vc, "linux");
}

/*
 * DA - DEVICE ATTRIBUTE
 */
static inline void vte_da(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	/* We claim VT220 compatibility... */
	sprintf(buf, "%s?62;1;2;6;7;8;9c", __VTE_CSI);
	puts_queue(vc, buf);
}

#define VTE_VERSION        211
/*
 * DA - SECONDARY DEVICE ATTRIBUTE [VT220 and up]
 *
 * Reply parameters:
 * 1 = Model (1=vt220, 18=vt330, 19=vt340, 41=vt420)
 * 2 = Firmware version (nn = n.n)
 * 3 = Installed options (0 = none)
 */
static void vte_dec_da2(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	sprintf(buf, "%s>%d;%d;0c", __VTE_CSI, 1, VTE_VERSION / 10);
	puts_queue(vc, buf);
}

/*
 * DA - TERTIARY DEVICE ATTRIBUTE [VT220 and up]
 *
 * Reply: unit ID (we report "0")
 */
static void vte_dec_da3(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	sprintf(buf, "%s!|%s%s", __VTE_DCS, "0", __VTE_ST);
	puts_queue(vc, buf);
}

/*
 * DECREPTPARM - DEC REPORT TERMINAL PARAMETERS [VT1xx/VT2xx/VT320]
 */
static void vte_decreptparm(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	sprintf(buf, "\033[%d;1;1;120;120;1;0x", vc->vc_par[0] + 2);
	puts_queue(vc, buf);
}

/*
 * SM - SET MODE /
 * RM - RESET MODE
 */
static void set_mode(struct vc_data *vc, int on_off)
{
	int i;

	for (i = 0; i <= vc->vc_npar; i++)
		/* DEC private modes set/reset */
		if (vc->vc_priv4)
			switch (vc->vc_par[i]) {
			case 1:	/* DECCKM - Cursor keys mode */
				if (on_off)
					set_kbd_mode(vc->kbd_table, VC_CKMODE);
				else
					clr_kbd_mode(vc->kbd_table, VC_CKMODE);
				break;
			case 2:	/* DECANM - ANSI mode */
				break;
			case 3:	/* DECCOLM -  Column mode */
#if 0
				deccolm = on_off;
				(void) vc_resize(vc->vc_rows, vc->vc_deccolm ? 132 : 80);
				/* this alone does not suffice; some user mode
				   utility has to change the hardware regs */
#endif
				break;
			case 4:	/* DECSCLM - Scrolling mode */
				break;
			case 5:	/* DECSCNM - Screen mode */
				if (vc->vc_decscnm != on_off) {
					vc->vc_decscnm = on_off;
					invert_screen(vc, 0, vc->vc_screenbuf_size, 0);
					update_attr(vc);
				}
				break;
			case 6:	/* DECOM - Origin mode */
				vc->vc_decom = on_off;
				gotoxay(vc, 0, 0);
				break;
			case 7:	/* DECAWM - Autowrap mode */
				vc->vc_decawm = on_off;
				break;
			case 8:	/* DECARM - Autorepeat mode */
				vc->vc_decarm = on_off;
				if (on_off)
					set_kbd_mode(vc->kbd_table, VC_REPEAT);
				else
					clr_kbd_mode(vc->kbd_table, VC_REPEAT);
				break;
			case 9:
				vc->vc_report_mouse = on_off ? 1 : 0;
				break;
			case 25:	/* DECTCEM - Text cursor enable mode */
				vc->vc_dectcem = on_off;
				break;
			case 42:	/* DECNCRS - National character set replacement mode */
				break;
			case 60:	/* DECHCCM - Horizontal cursor coupling mode */
				break;
			case 61:	/* DECVCCM - Vertical cursor coupling mode */
				break;
			case 64:	/* DECPCCM - Page cursor coupling mode */
				break;
			case 66:	/* DECNKM - Numeric keybad mode */
				vc->vc_decnkm = on_off;
				if (on_off)
					set_kbd_mode(vc->kbd_table, VC_APPLIC);
				else
					clr_kbd_mode(vc->kbd_table, VC_APPLIC);
				break;
			case 67:	/* DECBKM - Backarrow key mode */
				break;
			case 68:	/* DECKBUM - Keyboard usage mode */
				break;
			case 69:	/* DECVSSM - Vertical split screen mode
					 */
				break;
			case 73:	/* DECXRLM - Transfer rate limiting mode */
				break;
			case 81:	/* DECKPM - Keyboard position mode */
				break;
			case 1000:
				vc->vc_report_mouse = on_off ? 2 : 0;
				break;
		} else
			switch (vc->vc_par[i]) {	/* ANSI modes set/reset */
			case 3:	/* Monitor (display ctrls) */
				vc->vc_disp_ctrl = on_off;
				break;
			case 4:	/* Insert Mode on/off */
				vc->vc_irm = on_off;
				break;
			case 20:	/* Lf, Enter == CrLf/Lf */
				if (on_off)
					set_kbd_mode(vc->kbd_table, VC_CRLF);
				else
					clr_kbd_mode(vc->kbd_table, VC_CRLF);
				break;
			}
}

/*
 * DECCIR - Cursor information report
 */
static void vte_deccir(struct tty_struct *tty)
{
	/* not yet implemented */
}

/*
 * DECMSR - Macro space report
 */
static void vte_decmsr(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	sprintf(buf, "%s%d*{", __VTE_CSI, 0);	/* No space left */
	puts_queue(vc, buf);
}

/*
 * DECRPM - Report mode
 */
static void vte_decrpm(struct tty_struct *tty, int priv, int mode,
		       int status)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	char buf[40];

	if (status == 0) {
		status = 2;
	} else {
		if (status == 2) {
			status = 0;
		}
	}

	if (priv)
		sprintf(buf, "%s?%d;%d$y", __VTE_CSI, mode, status);
	else
		sprintf(buf, "%s%d;%d$y", __VTE_CSI, mode, status);
	puts_queue(vc, buf);
}

/*
 * DECRQM - Request mode
 *
 * Reply codes:
 * 0 = reset
 * 1 = set
 * 2 = unknown
 * 3 = premanently set
 * 4 = permanently reset
 */
static void vte_decrqm(struct tty_struct *tty, int priv)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (priv) {
		switch (vc->vc_par[0]) {
		case 1:	/* DECCKM - Cursor keys mode */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_decckm);
			break;
		case 2:	/* DECANM */
		case 3:	/* DECCOLM */
		case 4:	/* DECSCLM */
			vte_decrpm(tty, priv, vc->vc_par[0], 4);
			break;
		case 5:	/* DECSCNM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_decscnm);
			break;
		case 6:	/* DECOM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_decom);
			break;
		case 7:	/* DECAWM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_decawm);
			break;
		case 8:	/* DECARM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_decarm);
			break;
		case 25:	/* DECTCEM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_dectcem);
			break;
		case 42:	/* DECNCRM */
		case 60:	/* DECHCCM */
		case 61:	/* DECVCCM */
		case 64:	/* DECPCCM */
			vte_decrpm(tty, priv, vc->vc_par[0], 4);
			break;
		case 66:	/* DECNKM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_decnkm);
			break;
		case 67:	/* DECBKM */
		case 68:	/* DECKBUM */
		case 69:	/* DECVSSM */
		case 73:	/* DECXRLM */
		case 81:	/* DECKPM */
			vte_decrpm(tty, priv, vc->vc_par[0], 4);
			break;
		default:
			vte_decrpm(tty, priv, vc->vc_par[0], 2);
		}
	} else {
		switch (vc->vc_par[0]) {
		case 1:	/* GATM */
			vte_decrpm(tty, priv, vc->vc_par[0], 4);
			break;
		case 2:	/* KAM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_kam);
			break;
		case 3:	/* CRM */
			vte_decrpm(tty, priv, vc->vc_par[0], 4);
			break;
		case 4:	/* IRM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_irm);
			break;
		case 5:	/* SRTM */
		case 6:	/* ERM */
		case 7:	/* VEM */
		case 8:	/* BDSM */
		case 9:	/* DCSM */
		case 10:	/* HEM */
		case 11:	/* PUM */
		case 12:	/* SRM */
		case 13:	/* FEAM */
		case 14:	/* FETM */
		case 15:	/* MATM */
		case 16:	/* TTM */
		case 17:	/* SATM */
		case 18:	/* TSM */
		case 19:	/* EBM */
			vte_decrpm(tty, priv, vc->vc_par[0], 4);
			break;
		case 20:	/* LNM */
			vte_decrpm(tty, priv, vc->vc_par[0], vc->vc_lnm);
			break;
		case 21:	/* GRCM */
		case 22:	/* ZDM */
			vte_decrpm(tty, priv, vc->vc_par[0], 4);
			break;
		default:
			vte_decrpm(tty, priv, vc->vc_par[0], 2);
		}
	}
}

/*
 * DECSCL - Set operating level
 */
static void vte_decscl(struct vc_data *vc)
{
	switch (vc->vc_par[0]) {
	case 61:		/* VT100 mode */
		if (vc->vc_npar == 1) {
			vc->vc_decscl = 1;
			vc->vc_c8bit = 0;
		}
		break;
	case 62:		/* VT200 mode */
	case 63:		/* VT300 mode */
	case 64:		/* VT400 mode */
		if (vc->vc_npar <= 2) {
			vc->vc_decscl = 4;
			if (vc->vc_par[1] == 1)
				vc->vc_c8bit = 0;
			else
				vc->vc_c8bit = 1;
		}
		break;
	}
	return;
}

/*
 * DECTABSR - Tabulation stop report
 */
void vte_dectabsr(struct tty_struct *tty)
{
	/* not yet implemented */
}

/*
 * DECTSR - Terminal state report
 */
void vte_dectsr(struct tty_struct *tty)
{
	/* not yet implemented */
}

static void setterm_command(struct vc_data *vc)
{
	switch (vc->vc_par[0]) {
	case 1:		/* set color for underline mode */
		if (vc->vc_can_do_color && vc->vc_par[1] < 16) {
			vc->vc_ulcolor = color_table[vc->vc_par[1]];
			if (vc->vc_underline)
				update_attr(vc);
		}
		break;
	case 2:		/* set color for half intensity mode */
		if (vc->vc_can_do_color && vc->vc_par[1] < 16) {
			vc->vc_halfcolor = color_table[vc->vc_par[1]];
			if (vc->vc_intensity == 0)
				update_attr(vc);
		}
		break;
	case 8:		/* store colors as defaults */
		vc->vc_def_color = vc->vc_attr;
		if (vc->vc_hi_font_mask == 0x100)
			vc->vc_def_color >>= 1;
		default_attr(vc);
		update_attr(vc);
		break;
	case 9:		/* set blanking interval */
		vc->display_fg->blank_interval =
		    ((vc->vc_par[1] < 60) ? vc->vc_par[1] : 60) * 60 * HZ;
		poke_blanked_console(vc->display_fg);
		break;
	case 10:		/* set bell frequency in Hz */
		if (vc->vc_npar >= 1)
			vc->vc_bell_pitch = vc->vc_par[1];
		else
			vc->vc_bell_pitch = DEFAULT_BELL_PITCH;
		break;
	case 11:		/* set bell duration in msec */
		if (vc->vc_npar >= 1)
			vc->vc_bell_duration = (vc->vc_par[1] < 2000) ?
			    vc->vc_par[1] * HZ / 1000 : 0;
		else
			vc->vc_bell_duration = DEFAULT_BELL_DURATION;
		break;
	case 12:		/* bring specified console to the front */
		if (vc->vc_par[1] >= 0) {
			struct vc_data *tmp = find_vc(vc->vc_par[1]);
			set_console(tmp);
		}
		break;
	case 13:		/* unblank the screen */
		poke_blanked_console(vc->display_fg);
		break;
	case 14:		/* set vesa powerdown interval */
		vc->display_fg->off_interval =
		    ((vc->vc_par[1] < 60) ? vc->vc_par[1] : 60) * 60 * HZ;
		break;
	case 15:		/* activate the previous console */
		set_console(vc->display_fg->last_console);
		break;
	}
}

/*
 * ICH - INSERT CHARACTER [VT220]
 */
static void vte_ich(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_cols - vc->vc_x)
		nr = vc->vc_cols - vc->vc_x;
	else if (!nr)
		nr = 1;
	insert_char(vc, nr);
}

/*
 * IL - INSERT LINE
 */
static void vte_il(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_rows - vc->vc_y)
		nr = vc->vc_rows - vc->vc_y;
	else if (!nr)
		nr = 1;
	insert_line(vc, nr);
}

/*
 * DCH - DELETE CHARACTER
 */
static void vte_dch(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_cols - vc->vc_x)
		nr = vc->vc_cols - vc->vc_x;
	else if (!nr)
		nr = 1;
	delete_char(vc, nr);
}

/*
 * DL - DELETE LINE
 */
static void vte_dl(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_rows - vc->vc_y)
		nr = vc->vc_rows - vc->vc_y;
	else if (!nr)
		nr = 1;
	delete_line(vc, nr);
}

/*
 * DECSC - SAVE CURSOR
 *
 * This saves the following states:
 *  - cursor position
 *  - graphic rendition
 *  - character set shift state
 *  - state of wrap flag
 *  - state of origin mode
 *  - state of selective erase (not implemented)
 */
void vte_decsc(struct vc_data *vc)
{
	vc->vc_saved_x = vc->vc_x;
	vc->vc_saved_y = vc->vc_y;
	vc->vc_s_intensity = vc->vc_intensity;
	vc->vc_s_underline = vc->vc_underline;
	vc->vc_s_blink = vc->vc_blink;
	vc->vc_s_reverse = vc->vc_reverse;
	vc->vc_s_charset = vc->vc_charset;
	vc->vc_s_color = vc->vc_color;
	vc->vc_saved_G0 = vc->vc_G0_charset;
	vc->vc_saved_G1 = vc->vc_G1_charset;
	vc->vc_saved_G2 = vc->vc_G2_charset;
	vc->vc_saved_G3 = vc->vc_G3_charset;
}

/*
 * DECRC - RESTORE CURSOR
 */
static void vte_decrc(struct vc_data *vc)
{
	gotoxy(vc, vc->vc_saved_x, vc->vc_saved_y);
	vc->vc_intensity = vc->vc_s_intensity;
	vc->vc_underline = vc->vc_s_underline;
	vc->vc_blink = vc->vc_s_blink;
	vc->vc_reverse = vc->vc_s_reverse;
	vc->vc_charset = vc->vc_s_charset;
	vc->vc_color = vc->vc_s_color;
	vc->vc_G0_charset = vc->vc_saved_G0;
	vc->vc_G1_charset = vc->vc_saved_G1;
	vc->vc_G2_charset = vc->vc_saved_G2;
	vc->vc_G3_charset = vc->vc_saved_G3;
	set_translate(vc, vc->vc_charset ? vc->vc_G1_charset : vc->vc_G0_charset);
	update_attr(vc);
	vc->vc_need_wrap = 0;
}

/*
 * RIS - RESET TO INITIAL STATE
 *
 * On DEC terminals this causes the following:
 *  - all set-up parameters are replaced by power-up defaults
 *  - all communications lines are disconnected (should we send SIGHUP to
 *    controlling process?)
 *  - all user-defined keys are cleared (not implemented)
 *  - the screen is cleared
 *  - cursor is place to upper-left corner
 *  - SGR state is set to normal
 *  - selective erase attribute write state is set to "non-selective erase"
 *    (not implemented)
 *  - all character sets are set to default (not implemented)
 */
void vte_ris(struct vc_data *vc, int do_clear)
{
	vc->vc_top = 0;
	vc->vc_bottom = vc->vc_rows;
	vc->vc_state = ESinit;
	vc->vc_priv1 = 0;
	vc->vc_priv2 = 0;
	vc->vc_priv3 = 0;
	vc->vc_priv4 = 0;
	set_translate(vc, LAT1_MAP);
	vc->vc_G0_charset = LAT1_MAP;
	vc->vc_G1_charset = GRAF_MAP;
	vc->vc_charset = 0;
	vc->vc_need_wrap = 0;
	vc->vc_report_mouse = 0;
	vc->vc_utf = 0;
	vc->vc_utf_count = 0;

	vc->vc_disp_ctrl = 0;
	vc->vc_toggle_meta = 0;

	vc->vc_c8bit = 0;	/* disable 8-bit controls */
	vc->vc_decckm = 0;	/* cursor key sequences */
	vc->vc_decsclm = 0;	/* jump scroll */
	vc->vc_decscnm = 0;	/* normal screen */
	vc->vc_decom = 0;	/* absolute adressing */
	vc->vc_decawm = 1;	/* autowrap disabled */
	vc->vc_decarm = 1;	/* autorepeat enabled */
	vc->vc_dectcem = 1;	/* text cursor enabled */

	vc->vc_kam = 0;		/* keyboard enabled */
	vc->vc_crm = 0;		/* execute control functions */
	vc->vc_irm = 0;		/* replace */
	vc->vc_lnm = 0;		/* line feed */

	set_kbd_mode(vc->kbd_table, VC_REPEAT);
	clr_kbd_mode(vc->kbd_table, VC_CKMODE);
	clr_kbd_mode(vc->kbd_table, VC_APPLIC);
	clr_kbd_mode(vc->kbd_table, VC_CRLF);
	vc->kbd_table.lockstate = KBD_DEFLOCK;
	vc->kbd_table.slockstate = 0;
	vc->kbd_table.ledmode = LED_SHOW_FLAGS;
	vc->kbd_table.ledflagstate =
	    vc->kbd_table.default_ledflagstate = KBD_DEFLEDS;
	vc->kbd_table.modeflags = KBD_DEFMODE;
	vc->kbd_table.kbdmode = VC_XLATE;
	set_leds();

	vc->vc_cursor_type = CUR_DEFAULT;
	vc->vc_complement_mask = vc->vc_s_complement_mask;

	default_attr(vc);
	update_attr(vc);

	vc->vc_tab_stop[0] = 0x01010100;
	vc->vc_tab_stop[1] = vc->vc_tab_stop[2] = vc->vc_tab_stop[3] = vc->vc_tab_stop[4] = 0x01010101;

	vc->vc_bell_pitch = DEFAULT_BELL_PITCH;
	vc->vc_bell_duration = DEFAULT_BELL_DURATION;

	gotoxy(vc, 0, 0);
	vte_decsc(vc);
	if (do_clear)
		vte_ed(vc, 2);
}

/*
 * TABULATION CLEAR (TBC)
 *
 * NOTE:
 * In case of parameters 0 and 2 the number of lines affected depends on the
 * setting of the Tabulation Stop Mode (TSM).  Since we don't implement TSM,
 * this is silently ignored.
 *
 * Parameters 1 and 4 are similiar to 0 and 3, but affect only line tabulation
 * stops, which are not implemented.
 *
 * Parameter 2 may only be interpreted, when we implement a tabulation stop map
 * per display line.
 */
static void vte_tbc(struct vc_data *vc, int vpar)
{
	switch (vpar) {
	case 0:
		/*
		 * The character tabulation stop at the active
		 * presentation position is cleared.
		 */
		vc->vc_tab_stop[vc->vc_x >> 5] &= ~(1 << (vc->vc_x & 31));
		return;
	case 2:
		/*
		 * All character tabulation stops in the active
		 * line are cleared.
		 */
	case 3:
		/*
		 * All character tabulation stops are cleared.
		 */
	case 5:
		/*
		 * All tabulation stops are cleared.
		 */
		vc->vc_tab_stop[0] = vc->vc_tab_stop[1] = vc->vc_tab_stop[2] =
			vc->vc_tab_stop[3] = vc->vc_tab_stop[4] = 0;
	}
}

void terminal_emulation(struct tty_struct *tty, int c)
{
	/*
	 * C0 CONTROL CHARACTERS
	 *
	 * NOTE: Control characters can be used in the _middle_
	 *       of an escape sequence.  (XXX: Really? Test!)
	 */
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	switch (c) {
	case 0x00:		/* NUL - Null */
	case 0x01:		/* SOH - Start of header */
	case 0x02:		/* STX - */
	case 0x03:		/* ETX - */
	case 0x04:		/* EOT - End of transmission */
		return;
	case 0x05:		/* ENQ - Enquiry */
		vte_answerback(tty);
		return;
	case 0x06:		/* ACK - Acknowledge */
		return;
	case 0x07:		/* BEL - Bell */
		if (vc->vc_bell_duration)
			kd_mksound(vc->display_fg->beeper, vc->vc_bell_pitch, vc->vc_bell_duration);
		return;
	case 0x08:		/* BS - Back space */
		vte_bs(vc);
		return;
	case 0x09:		/* HT - Character tabulation */
		vc->vc_pos -= (vc->vc_x << 1);
		while (vc->vc_x < vc->vc_cols - 1) {
			vc->vc_x++;
			if (vc->vc_tab_stop[vc->vc_x >> 5] & (1 << (vc->vc_x & 31)))
				break;
		}
		vc->vc_pos += (vc->vc_x << 1);
		return;
	case 0x0a:		/* LF - Line feed */
	case 0x0b:		/* VT - Line tabulation */
		/*
		 * Since line tabulation is not implemented in the DEC VT
		 * series (except VT131 ?),  the DEC VT series treats any
		 * VT as LF.
		 */
	case 0x0c:		/* FF - Form feed */
		/*
		 * DEC VT series processes FF as LF.
		 */
		vte_lf(vc);
		if (!get_kbd_mode(vc->kbd_table, VC_CRLF))
			return;
	case 0x0d:		/* CR - Carriage return */
		vte_cr(vc);
		return;
	case 0x0e:		/* SO - Shift out / LS1 - Locking shift 1 */
		vc->vc_charset = 1;
		set_translate(vc, vc->vc_G1_charset);
		vc->vc_disp_ctrl = 1;
		return;
	case 0x0f:		/* SI - Shift in / LS0 - Locking shift 0 */
		vc->vc_charset = 0;
		set_translate(vc, vc->vc_G0_charset);
		vc->vc_disp_ctrl = 0;
		return;
	case 0x10:		/* DLE - */
	case 0x11:		/* DC1 - Device control 1 */
	case 0x12:		/* DC2 - Device control 1 */
	case 0x13:		/* DC3 - Device control 1 */
	case 0x14:		/* DC4 - Device control 1 */
	case 0x15:		/* NAK - Negative acknowledge */
	case 0x16:		/* SYN - Synchronize */
	case 0x17:		/* ETB - */
		return;
	case 0x18:		/* CAN - Cancel */
		vc->vc_state = ESinit;
		return;
	case 0x19:		/* EM - */
		return;
	case 0x1a:		/* SUB - Substitute */
		vc->vc_state = ESinit;
		return;
	case 0x1b:		/* ESC - Escape */
		vc->vc_state = ESesc;
		return;
	case 0x1c:		/* IS4 - */
	case 0x1d:		/* IS3 - */
	case 0x1e:		/* IS2 - */
	case 0x1f:		/* IS1 - */
		return;
	case 0x7f:		/* DEL - Delete */
		/*
		 * This character is ignored, unless a 96-set has been mapped,
		 * but this is not supported at the moment.
		 */
		return;
	}

	if (vc->vc_c8bit == 1)
		/*
		 * C1 control functions (8-bit mode).
		 */
		switch (c) {
		case 0x80:	/* unused */
		case 0x81:	/* unused */
		case 0x82:	/* BPH - Break permitted here */
		case 0x83:	/* NBH - No break here */
			return;
		case 0x84:	/* IND - Line feed (DEC only) */
#ifndef VTE_STRICT_ISO
			vte_lf(vc);
#endif				/* ndef VTE_STRICT_ISO */
			return;
		case 0x85:	/* NEL - Next line */
			vte_lf(vc);
			vte_cr(vc);
			return;
		case 0x86:	/* SSA - Start of selected area */
		case 0x87:	/* ESA - End of selected area */
			return;
		case 0x88:	/* HTS - Character tabulation set */
			vc->vc_tab_stop[vc->vc_x >> 5] |= (1 << (vc->vc_x & 31));
			return;
		case 0x89:	/* HTJ - Character tabulation with justify */
		case 0x8a:	/* VTS - Line tabulation set */
		case 0x8b:	/* PLD - Partial line down */
		case 0x8c:	/* PLU - Partial line up */
			return;
		case 0x8d:	/* RI - Reverse line feed */
			vte_ri(vc);
			return;
#if 0
		case 0x8e:	/* SS2 - Single shift 2 */
			vc->vc_need_shift = 1;
			vc->vc_GS_charset = vc->vc_G2_charset;	/* G2 -> GS */
			return;
		case 0x8f:	/* SS3 - Single shift 3 */
			vc->vc_need_shift = 1;
			vc->vc_GS_charset = vc->vc_G3_charset;	/* G3 -> GS */
			return;
#endif
		case 0x90:	/* DCS - Device control string */
			return;
		case 0x91:	/* PU1 - Private use 1 */
		case 0x92:	/* PU2 - Private use 2 */
		case 0x93:	/* STS - Set transmit state */
		case 0x94:	/* CCH - Cancel character */
		case 0x95:	/* MW  - Message waiting */
		case 0x96:	/* SPA - Start of guarded area */
		case 0x97:	/* EPA - End of guarded area */
		case 0x98:	/* SOS - Start of string */
		case 0x99:	/* unused */
			return;
		case 0x9a:	/* SCI - Single character introducer */
#ifndef VTE_STRICT_ISO
			vte_da(tty);
#endif				/* ndef VTE_STRICT_ISO */
			return;
		case 0x9b:	/* CSI - Control sequence introducer */
			vc->vc_state = EScsi;
			return;
		case 0x9c:	/* ST  - String Terminator */
		case 0x9d:	/* OSC - Operating system command */
		case 0x9e:	/* PM  - Privacy message */
		case 0x9f:	/* APC - Application program command */
			return;
		}

	switch (vc->vc_state) {
	case ESesc:
		vc->vc_state = ESinit;
		switch (c) {

		case ' ':	/* ACS - Announce code structure */
			vc->vc_state = ESacs;
			return;
		case '#':	/* SCF - Single control functions */
			vc->vc_state = ESscf;
			return;
		case '%':	/* DOCS - Designate other coding system */
			vc->vc_state = ESdocs;
			return;
#ifdef CONFIG_VT_HP
		case '&':	/* HP terminal emulation */
			vc->vc_state = ESesc_and;
			return;
#endif				/* def CONFIG_VT_HP */
		case '(':	/* GZD4 - G0-designate 94-set */
			vc->vc_state = ESgzd4;
			return;
		case ')':	/* G1D4 - G1-designate 94-set */
			vc->vc_state = ESg1d4;
			return;
#if 0
		case '*':	/* G2D4 - G2-designate 94-set */
			vc->vc_state = ESg2d4;
			return;
		case '+':	/* G3D4 - G3-designate 94-set */
			vc->vc_state = ESg3d4;
			return;
		case '-':	/* G1D6 - G1-designate 96-set */
			vc->vc_state = ESg1d6;
			return;
		case '.':	/* G2D6 - G2-designate 96-set */
			vc->vc_state = ESg2d6;
			return;
		case '/':	/* G3D6 - G3-designate 96-set */
			vc->vc_state = ESg3d6;
			return;
#endif
			/* ===== Private control functions ===== */

		case '6':	/* DECBI - Back index */
			return;
		case '7':	/* DECSC - Save cursor */
			vte_decsc(vc);
			return;
		case '8':	/* DECRC - Restore cursor */
			vte_decrc(vc);
			return;
		case '9':	/* DECFI - Forward index */
			return;
		case '=':	/* DECKPAM - Keypad application mode */
			vc->vc_decnkm = 1;
			set_kbd_mode(vc->kbd_table, VC_APPLIC);
			return;
		case '>':	/* DECKPNM - Keypad numeric mode */
			vc->vc_decnkm = 0;
			clr_kbd_mode(vc->kbd_table, VC_APPLIC);
			return;

			/* ===== C1 control functions ===== */
		case '@':	/* unallocated */
		case 'A':	/* unallocated */
		case 'B':	/* BPH - Break permitted here */
		case 'C':	/* NBH - No break here */
		case 'D':	/* IND - Line feed (DEC only) */
#ifndef VTE_STRICT_ISO
			vte_lf(vc);
#endif				/* ndef VTE_STRICT_ISO */
			return;
		case 'E':	/* NEL - Next line */
			vte_cr(vc);
			vte_lf(vc);
			return;
		case 'F':	/* SSA - Start of selected area */
		case 'G':	/* ESA - End of selected area */
			return;
		case 'H':	/* HTS - Character tabulation set */
			vc->vc_tab_stop[vc->vc_x >> 5] |= (1 << (vc->vc_x & 31));
			return;
		case 'I':	/* HTJ - Character tabulation with justify */
		case 'J':	/* VTS - Line tabulation set */
		case 'K':	/* PLD - Partial line down */
		case 'L':	/* PLU - Partial line up */
			return;
		case 'M':	/* RI - Reverse line feed */
			vte_ri(vc);
			return;
		case 'N':	/* SS2 - Single shift 2 */
			vc->vc_shift = 1;
			vc->vc_GS_charset = vc->vc_G2_charset;	/* G2 -> GS */
			return;
		case 'O':	/* SS3 - Single shift 3 */
			vc->vc_shift = 1;
			vc->vc_GS_charset = vc->vc_G3_charset;
			return;
		case 'P':	/* DCS - Device control string */
			return;
		case 'Q':	/* PU1 - Private use 1 */
		case 'R':	/* PU2 - Private use 2 */
		case 'S':	/* STS - Set transmit state */
		case 'T':	/* CCH - Cancel character */
		case 'U':	/* MW - Message waiting */
		case 'V':	/* SPA - Start of guarded area */
		case 'W':	/* EPA - End of guarded area */
		case 'X':	/* SOS - Start of string */
		case 'Y':	/* unallocated */
			return;
		case 'Z':	/* SCI - Single character introducer */
#ifndef VTE_STRICT_ISO
			vte_da(tty);
#endif				/* ndef VTE_STRICT_ISO */
			return;
		case '[':	/* CSI - Control sequence introducer */
			vc->vc_state = EScsi;
			return;
		case '\\':	/* ST  - String Terminator */
			return;
		case ']':	/* OSC - Operating system command */
			/* XXX: Fixme! Wrong sequence and format! */
			vc->vc_state = ESosc;
			return;
		case '^':	/* PM  - Privacy Message */
		case '_':	/* APC - Application Program Command */
			return;

			/* ===== Single control functions ===== */
		case '`':	/* DMI - Disable manual input */
			vc->vc_kam = 0;
			return;
		case 'b':	/* EMI - Enable manual input */
			vc->vc_kam = 1;
			return;
		case 'c':	/* RIS - Reset ti initial state */
			vte_ris(vc, 1);
			return;
		case 'd':	/* CMD - Coding Method Delimiter */
			return;
#if 0
		case 'n':	/* LS2 - Locking shift G2 */
			GL_charset = vc->vc_G2_charset;	/*  (G2 -> GL) */
			return;
		case 'o':	/* LS3 - Locking shift G3 */
			GL_charset = vc->vc_G3_charset;	/*  (G3 -> GL) */
			return;
		case '|':	/* LS3R - Locking shift G3 right */
			GR_charset = vc->vc_G3_charset;	/* G3 -> GR */
			return;
		case '}':	/* LS2R - Locking shift G2 right */
			GR_charset = vc->vc_G2_charset;	/* G2 -> GR */
			return;
		case '~':	/* LS1R - Locking shift G1 right */
			GR_charset = vc->vc_G1_charset;	/* G1 -> GR */
			return;
#endif
		}
		return;
	case ESacs:
		vc->vc_state = ESinit;
		switch (c) {
		case 'F':	/* Select 7-bit C1 control transmission */
			if (vc->vc_decscl != 1)	/* Ignore if in VT100 mode */
				vc->vc_c8bit = 0;
			return;
		case 'G':	/* Select 8-Bit C1 control transmission */
			if (vc->vc_decscl != 1)	/* Ignore if in VT100 mode */
				vc->vc_c8bit = 1;
			return;
		case 'L':	/* ANSI conformance level 1 */
		case 'M':	/* ANSI conformance level 2 */
		case 'N':	/* ANSI conformance level 3 */
			/* Not yet implemented. */
			return;
		}
		return;
	case ESosc:
		vc->vc_state = ESinit;
		switch (c) {
		case 'P':	/* palette escape sequence */
			for (vc->vc_npar = 0; vc->vc_npar < NPAR; vc->vc_npar++)
				vc->vc_par[vc->vc_npar] = 0;
			vc->vc_npar = 0;
			vc->vc_state = ESpalette;
			return;
		case 'R':	/* reset palette */
			reset_palette(vc);
			vc->vc_state = ESinit;
			return;
		}
		return;
	case ESpalette:
		if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')
		    || (c >= 'a' && c <= 'f')) {
			vc->vc_par[vc->vc_npar++] =
			    (c > '9' ? (c & 0xDF) - 'A' + 10 : c - '0');
			if (vc->vc_npar == 7) {
				int i = vc->vc_par[0] * 3, j = 1;
				vc->vc_palette[i] = 16 * vc->vc_par[j++];
				vc->vc_palette[i++] += vc->vc_par[j++];
				vc->vc_palette[i] = 16 * vc->vc_par[j++];
				vc->vc_palette[i++] += vc->vc_par[j++];
				vc->vc_palette[i] = 16 * vc->vc_par[j++];
				vc->vc_palette[i] += vc->vc_par[j];
				set_palette(vc);
				vc->vc_state = ESinit;
			}
		} else
			vc->vc_state = ESinit;
		return;
	case EScsi:
		for (vc->vc_npar = 0; vc->vc_npar < NPAR; vc->vc_npar++)
			vc->vc_par[vc->vc_npar] = 0;
		vc->vc_npar = 0;
		vc->vc_state = EScsi_getpars;
		if (c == '[') {
			/* Function key */
			vc->vc_state = ESfunckey;
			return;
		}
		vc->vc_priv1 = (c == '<');
		vc->vc_priv2 = (c == '=');
		vc->vc_priv3 = (c == '>');
		vc->vc_priv4 = (c == '?');
		if (vc->vc_priv1) {
			vc->vc_state = ESinit;
			return;
		}
		if (vc->vc_priv2 || vc->vc_priv3 || vc->vc_priv4) {
			return;
		}
	case EScsi_getpars:
		if (c == ';' && vc->vc_npar < NPAR - 1) {
			vc->vc_npar++;
			return;
		} else if (c >= '0' && c <= '9') {
			vc->vc_par[vc->vc_npar] *= 10;
			vc->vc_par[vc->vc_npar] += c - '0';
			return;
		} else
			vc->vc_state = EScsi_gotpars;
	case EScsi_gotpars:
		vc->vc_state = ESinit;
		/*
		 * Process control functions  with private parameter flag.
		 */
		switch (c) {
		case '$':
			if (vc->vc_priv4) {
				vc->vc_state = EScsi_dollar;
				return;
			}
			break;
		case 'J':
			if (vc->vc_priv4) {
				/* DECSED - Selective erase in display */
				return;
			}
			break;
		case 'K':
			if (vc->vc_priv4) {
				/* DECSEL - Selective erase in display */
				return;
			}
			break;
		case 'h':	/* SM - Set Mode */
			set_mode(vc, 1);
			return;
		case 'l':	/* RM - Reset Mode */
			set_mode(vc, 0);
			return;
		case 'c':
			if (vc->vc_priv2) {
				if (!vc->vc_par[0])
					vte_dec_da3(tty);
				vc->vc_priv2 = 0;
				return;
			}
			if (vc->vc_priv3) {
				if (!vc->vc_par[0])
					vte_dec_da2(tty);
				vc->vc_priv3 = 0;
				return;
			}
			if (vc->vc_priv4) {
				if (vc->vc_par[0])
					vc->vc_cursor_type = vc->vc_par[0] | (vc->vc_par[1] << 8) | (vc->vc_par[2] << 16);
				else
					vc->vc_cursor_type = CUR_DEFAULT;
				vc->vc_priv4 = 0;
				return;
			}
			break;
		case 'm':
			if (vc->vc_priv4) {
				clear_selection();
				if (vc->vc_par[0])
					vc->vc_complement_mask =
					    vc->vc_par[0] << 8 | vc->vc_par[1];
				else
					vc->vc_complement_mask =
					    vc->vc_s_complement_mask;
				vc->vc_priv4 = 0;
				return;
			}
			break;
		case 'n':
			if (vc->vc_priv4) {
				switch (vc->vc_par[0]) {
				case 6:	/* DECXCPR - Extended CPR */
					vte_cpr(tty, 1);
					break;
				case 15:	/* DEC printer status */
					vte_fake_dec_dsr(tty, "13");
					break;
				case 25:	/* DEC UDK status */
					vte_fake_dec_dsr(tty, "21");
					break;
				case 26:	/* DEC keyboard status */
					vte_fake_dec_dsr(tty, "27;1;0;1");
					break;
				case 53:	/* DEC locator status */
					vte_fake_dec_dsr(tty, "53");
					break;
				case 62:	/* DEC macro space */
					vte_decmsr(tty);
					break;
				case 75:	/* DEC data integrity */
					vte_fake_dec_dsr(tty, "70");
					break;
				case 85:	/* DEC multiple session status */
					vte_fake_dec_dsr(tty, "83");
					break;
				}
			} else
				switch (vc->vc_par[0]) {
				case 5:	/* DSR - Device status report */
					vte_dsr(tty);
					break;
				case 6:	/* CPR - Cursor position report */
					vte_cpr(tty, 0);
					break;
				}
			vc->vc_priv4 = 0;
			return;
		}
		if (vc->vc_priv1 || vc->vc_priv2 || vc->vc_priv3 || vc->vc_priv4) {
			vc->vc_priv1 = vc->vc_priv2 = vc->vc_priv3 = vc->vc_priv4 = 0;
			return;
		}
		/*
		 * Process control functions with standard parameter strings.
		 */
		switch (c) {

			/* ===== Control functions w/ intermediate byte ===== */
		case ' ':	/* Intermediate byte: SP (ISO 6429) */
			vc->vc_state = EScsi_space;
			return;
		case '!':	/* Intermediate byte: ! (DEC VT series) */
			vc->vc_state = EScsi_exclam;
			return;
		case '"':	/* Intermediate byte: " (DEC VT series) */
			vc->vc_state = EScsi_dquote;
			return;
		case '$':	/* Intermediate byte: $ (DEC VT series) */
			vc->vc_state = EScsi_dollar;
			return;
		case '&':	/* Intermediate byte: & (DEC VT series) */
			vc->vc_state = EScsi_and;
			return;
		case '*':	/* Intermediate byte: * (DEC VT series) */
			vc->vc_state = EScsi_star;
			return;
		case '+':	/* Intermediate byte: + (DEC VT series) */
			vc->vc_state = EScsi_plus;
			return;
			/* ==== Control functions w/o intermediate byte ==== */
		case '@':	/* ICH - Insert character */
			vte_ich(vc, vc->vc_par[0]);
			return;
		case 'A':	/* CUU - Cursor up */
		case 'k':	/* VPB - Line position backward */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, vc->vc_x, vc->vc_y - vc->vc_par[0]);
			return;
		case 'B':	/* CUD - Cursor down */
		case 'e':	/* VPR - Line position forward */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, vc->vc_x, vc->vc_y + vc->vc_par[0]);
			return;
		case 'C':	/* CUF - Cursor right */
		case 'a':	/* HPR - Character position forward */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, vc->vc_x + vc->vc_par[0], vc->vc_y);
			return;
		case 'D':	/* CUB - Cursor left */
		case 'j':	/* HPB - Character position backward */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, vc->vc_x - vc->vc_par[0], vc->vc_y);
			return;
		case 'E':	/* CNL - Cursor next line */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, 0, vc->vc_y + vc->vc_par[0]);
			return;
		case 'F':	/* CPL - Cursor preceeding line */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, 0, vc->vc_y - vc->vc_par[0]);
			return;
		case 'G':	/* CHA - Cursor character absolute */
		case '`':	/* HPA - Character position absolute */
			if (vc->vc_par[0])
				vc->vc_par[0]--;
			gotoxy(vc, vc->vc_par[0], vc->vc_y);
			return;
		case 'H':	/* CUP - Cursor position */
		case 'f':	/* HVP - Horizontal and vertical position */
			if (vc->vc_par[0])
				vc->vc_par[0]--;
			if (vc->vc_par[1])
				vc->vc_par[1]--;
			gotoxay(vc, vc->vc_par[1], vc->vc_par[0]);
			return;
		case 'I':	/* CHT - Cursor forward tabulation */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			vte_cht(vc, vc->vc_par[0]);
			return;
		case 'J':	/* ED - Erase in page */
			vte_ed(vc, vc->vc_par[0]);
			return;
		case 'K':	/* EL - Erase in line */
			vte_el(vc, vc->vc_par[0]);
			return;
		case 'L':	/* IL - Insert line */
			vte_il(vc, vc->vc_par[0]);
			return;
		case 'M':	/* DL - Delete line */
			vte_dl(vc, vc->vc_par[0]);
			return;
		case 'P':	/* DCH - Delete character */
			vte_dch(vc, vc->vc_par[0]);
			return;
		case 'U':	/* NP - Next page */
		case 'V':	/* PP - Preceeding page */
			return;
		case 'W':	/* CTC - Cursor tabulation control */
			switch (vc->vc_par[0]) {
			case 0:	/* Set character tab stop at current position */
				vc->vc_tab_stop[vc->vc_x >> 5] |= (1 << (vc->vc_x & 31));
				return;
			case 2:	/* Clear character tab stop at curr. position */
				vte_tbc(vc, 0);
				return;
			case 5:	/* All character tab stops are cleared. */
				vte_tbc(vc, 5);
				return;
			}
			return;
		case 'X':	/* ECH - Erase character */
			vte_ech(vc, vc->vc_par[0]);
			return;
		case 'Y':	/* CVT - Cursor line tabulation */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			vte_cvt(vc, vc->vc_par[0]);
			return;
		case 'Z':	/* CBT - Cursor backward tabulation */
			vte_cbt(vc, vc->vc_par[0]);
			return;
		case ']':
#ifndef VT_STRICT_ISO
			setterm_command(vc);
#endif				/* def VT_STRICT_ISO */
			return;
		case 'c':	/* DA - Device attribute */
			if (!vc->vc_par[0])
				vte_da(tty);
			return;
		case 'd':	/* VPA - Line position absolute */
			if (vc->vc_par[0])
				vc->vc_par[0]--;
			gotoxay(vc, vc->vc_x, vc->vc_par[0]);
			return;
		case 'g':	/* TBC - Tabulation clear */
			vte_tbc(vc, vc->vc_par[0]);
			return;
		case 'm':	/* SGR - Select graphics rendition */
			vte_sgr(vc);
			return;

			/* ===== Private control sequences ===== */

		case 'q':	/* DECLL - but only 3 leds */
			switch (vc->vc_par[0]) {
			case 0:	/* all LEDs off */
			case 1:	/* LED 1 on */
			case 2:	/* LED 2 on */
			case 3:	/* LED 3 on */
				setledstate(vc, (vc->vc_par[0] < 3) ? vc->vc_par[0] : 4);
			case 4:	/* LED 4 on */
				;
			}
			return;
		case 'r':	/* DECSTBM - Set top and bottom margin */
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			if (!vc->vc_par[1])
				vc->vc_par[1] = vc->vc_rows;
			/* Minimum allowed region is 2 lines */
			if (vc->vc_par[0] < vc->vc_par[1] && vc->vc_par[1] <= vc->vc_rows) {
				vc->vc_top = vc->vc_par[0] - 1;
				vc->vc_bottom = vc->vc_par[1];
				gotoxay(vc, 0, 0);
			}
			return;
		case 's':	/* DECSLRM - Set left and right margin */
			return;
		case 't':	/* DECSLPP - Set lines per page */
			return;
		case 'x':	/* DECREQTPARM - Request terminal parameters */
			vte_decreptparm(tty);
			return;
		case 'y':
			if (vc->vc_par[0] == 4) {
				/* DECTST - Invoke confidence test */
				return;
			}
		}
		return;
	case EScsi_space:
		vc->vc_state = ESinit;
		switch (c) {
			/*
			 * Note: All codes betweem 0x40 and 0x6f are subject to
			 * standardisation by the ISO. The codes netweem 0x70
			 * and 0x7f are free for private use.
			 */
		case '@':	/* SL - Scroll left */
		case 'A':	/* SR - Scroll right */
			return;
		case 'P':	/* PPA - Page position absolute */
		case 'Q':	/* PPR - Page position forward */
		case 'R':	/* PPB - Page position backward */
			return;
		}
		return;
	case EScsi_exclam:
		vc->vc_state = ESinit;
		switch (c) {
		case 'p':	/* DECSTR - Soft terminal reset */
			/*
			 * Note: On a true DEC VT there are differences
			 * between RIS and DECSTR. Right now we ignore
			 * this... -dbk
			 */
			vte_ris(vc, 1);
			return;
		}
		return;
	case EScsi_dquote:
		vc->vc_state = ESinit;
		switch (c) {
		case 'p':	/* DECSCL - Set operating level */
			vte_decscl(vc);
			return;
		case 'q':	/* DECSCA - Select character protection
				   attribute */
			return;
		case 'v':	/* DECRQDE - Request window report */
			;
		}
		return;
	case EScsi_dollar:
		vc->vc_state = ESinit;
		switch (c) {
		case 'p':	/* DECRQM - Request mode */
			vte_decrqm(tty, vc->vc_priv4);
			return;
		case 'r':	/* DECCARA - Change attributes in rectangular area */
			return;
		case 't':	/* DECRARA - Reverse attributes in rectangular area */
			return;
		case 'u':	/* DECRQTSR - Request terminal state */
			if (vc->vc_par[0] == 1)
				vte_dectsr(tty);
			return;
		case 'v':	/* DECCRA - Copy rectangular area */
			return;
		case 'w':	/* DECRQPSR - Request presentation status */
			switch (vc->vc_par[0]) {
			case 1:
				vte_deccir(tty);
				break;
			case 2:
				vte_dectabsr(tty);
				break;
			}
			return;
		case 'x':	/* DECFRA - Fill rectangular area */
			return;
		case 'z':	/* DECERA - Erase rectangular area */
			return;
		case '{':	/* DECSERA - Selective erase rectangular area */
			return;
		case '|':	/* DECSCPP - Set columns per page */
			return;
		case '}':	/* DECSASD - Select active status  display */
			return;
		case '~':	/* DECSSDT - Select status display type
				 */
			return;
		}
		return;
	case EScsi_and:
		vc->vc_state = ESinit;
		switch (c) {
		case 'u':	/* DECRQUPSS - Request user-preferred supplemental set */
			return;
		case 'x':	/* Enable Session Command */
			return;
		}
		return;
	case EScsi_squote:
		vc->vc_state = ESinit;
		switch (c) {
		case '}':	/* DECIC - Insert column */
			return;
		case '~':	/* DECDC - Delete column */
			return;
		}
	case EScsi_star:
		vc->vc_state = ESinit;
		switch (c) {
		case 'x':	/* DECSACE - Select attribute change extent */
			return;
		case 'y':	/* DECRQCRA - Request checksum on rectangular area */
			return;
		case 'z':	/* DECINVM - Invoke macro */
			return;
		case '|':	/* DECSNLS - Select number of lines */
			return;
		case '}':	/* DECLFKC - Local function key control
				 */
			return;
		}
		return;
	case EScsi_plus:
		vc->vc_state = ESinit;
		switch (c) {
		case 'p':	/* DECSR - Secure reset */
			return;
		}
		return;
	case ESdocs:
		vc->vc_state = ESinit;
		switch (c) {
		case '@':	/* defined in ISO 2022 */
			vc->vc_utf = 0;
			return;
		case 'G':	/* prelim official escape code */
		case '8':	/* retained for compatibility */
			vc->vc_utf = 1;
			return;
		}
		return;
#ifdef CONFIG_VT_HP
	case ESesc_and:
		vc->vc_state = ESinit;
		switch (c) {
		case 'f':	/* Set function key label */
			return;
		case 'j':	/* Display function key labels */
			return;
		}
		return;
#endif
	case ESfunckey:
		vc->vc_state = ESinit;
		return;
	case ESscf:
		vc->vc_state = ESinit;
		if (c == '8') {
			/* DEC screen alignment test. kludge :-) */
			vc->vc_video_erase_char = (vc->vc_video_erase_char & 0xff00) | 'E';
			vte_ed(vc, 2);
			vc->vc_video_erase_char = (vc->vc_video_erase_char & 0xff00) | ' ';
			do_update_region(vc, vc->vc_origin, vc->vc_screenbuf_size / 2);
		}
		return;
	case ESgzd4:
		switch (c) {
		case '0':	/* DEC Special graphics */
			vc->vc_G0_charset = GRAF_MAP;
			break;
#if 0
		case '>':	/* DEC Technical */
			vc->vc_G0_charset = DEC_TECH_MAP;
			break;
#endif
		case 'A':	/* ISO Latin-1 supplemental */
			vc->vc_G0_charset = LAT1_MAP;
			break;
		case 'B':	/* ASCII */
			vc->vc_G0_charset = LAT1_MAP;
			break;
		case 'U':
			vc->vc_G0_charset = IBMPC_MAP;
			break;
		case 'K':
			vc->vc_G0_charset = USER_MAP;
			break;
		}
		if (vc->vc_charset == 0)
			set_translate(vc, vc->vc_G0_charset);
		vc->vc_state = ESinit;
		return;
	case ESg1d4:
		switch (c) {
		case '0':	/* DEC Special graphics */
			vc->vc_G1_charset = GRAF_MAP;
			break;
#if 0
		case '>':	/* DEC Technical */
			vc->vc_G1_charset = DEC_TECH_MAP;
			break;
#endif
		case 'A':	/* ISO Latin-1 supplemental */
			vc->vc_G1_charset = LAT1_MAP;
			break;
		case 'B':	/* ASCII */
			vc->vc_G1_charset = LAT1_MAP;
			break;
		case 'U':
			vc->vc_G1_charset = IBMPC_MAP;
			break;
		case 'K':
			vc->vc_G1_charset = USER_MAP;
			break;
		}
		if (vc->vc_charset == 1)
			set_translate(vc, vc->vc_G1_charset);
		vc->vc_state = ESinit;
		return;
	case ESg2d4:
		switch (c) {
		case '0':	/* DEC Special graphics */
			vc->vc_G2_charset = GRAF_MAP;
			break;
#if 0
		case '>':	/* DEC Technical */
			vc->vc_G2_charset = DEC_TECH_MAP;
			break;
#endif
		case 'A':	/* ISO Latin-1 supplemental */
			vc->vc_G2_charset = LAT1_MAP;
			break;
		case 'B':	/* ASCII */
			vc->vc_G2_charset = LAT1_MAP;
			break;
		case 'U':
			vc->vc_G2_charset = IBMPC_MAP;
			break;
		case 'K':
			vc->vc_G2_charset = USER_MAP;
			break;
		}
		if (vc->vc_charset == 1)
			set_translate(vc, vc->vc_G2_charset);
		vc->vc_state = ESinit;
		return;
	case ESg3d4:
		switch (c) {
		case '0':	/* DEC Special graphics */
			vc->vc_G3_charset = GRAF_MAP;
			break;
#if 0
		case '>':	/* DEC Technical */
			vc->vc_G3_charset = DEC_TECH_MAP;
			break;
#endif
		case 'A':	/* ISO Latin-1 supplemental */
			vc->vc_G3_charset = LAT1_MAP;
			break;
		case 'B':	/* ASCII */
			vc->vc_G3_charset = LAT1_MAP;
			break;
		case 'U':
			vc->vc_G3_charset = IBMPC_MAP;
			break;
		case 'K':
			vc->vc_G3_charset = USER_MAP;
			break;
		}
		if (vc->vc_charset == 1)
			set_translate(vc, vc->vc_G3_charset);
		vc->vc_state = ESinit;
		return;
	default:
		vc->vc_state = ESinit;
	}
}

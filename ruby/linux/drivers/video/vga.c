/*
 * linux/drivers/video/vga.c -- Core vga routines for vgacon and vga16fb
 *
 * Copyright 2000 James Simmons <jsimmons@linux-fbdev.org>
 *
 * Based on code from both vgacon and the VGA 16 framebuffer device.
 * This code is meant to be shared between vga16fb and vgacon. Also 
 * This code will help the ability to go from vgacon to fbdev to vgacon
 * again.
 *
 * This file is subject to the terms and conditions of the GNU General 
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/kd.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <video/vga.h>

static spinlock_t vga_lock = SPIN_LOCK_UNLOCKED;

void vga_clock_chip(struct vga_hw_state *state, unsigned int pixclock, int mul,
                    int div)
{
        static struct {
                u32 pixclock;
                u8  misc;
                u8  seq_clock_mode;
        } *ptr, *best, vgaclocks[] = {
                { 79442 /* 12.587 */, 0x00, 0x08},
                { 70616 /* 14.161 */, 0x04, 0x08},
                { 39721 /* 25.175 */, 0x00, 0x00},
                { 35308 /* 28.322 */, 0x04, 0x00},
                {     0 /* bad */,    0x00, 0x00}};
        int err;

        pixclock = (pixclock * mul) / div;
        best = vgaclocks;
        err = pixclock - best->pixclock;
        if (err < 0) err = -err;
        for (ptr = vgaclocks + 1; ptr->pixclock; ptr++) {
                int tmp;

	 	tmp = pixclock - ptr->pixclock;
                if (tmp < 0) tmp = -tmp;
                if (tmp < err) {
                        err = tmp;
                        best = ptr;
                }
        }
        state->misc |= best->misc;
        state->clkdiv = best->seq_clock_mode;
        pixclock = (best->pixclock * div) / mul;
}

#define FAIL(X) return -EINVAL

int vga_check_mode(xres, vxres, right, hslen, left, yres, lower, vslen,
                   upper, double_scan)
{
	int ytotal;

        if (hslen >= 32)
                FAIL("hslen too big");
        if (right + hslen + left > 64)
                FAIL("hblank too big");
        if (xres + right + hslen + left >= 256)
                FAIL("xtotal too big");

	if (double_scan) {
		yres  <<= 1;
                lower <<= 1;
                vslen <<= 1;
                upper <<= 1;
        }

	ytotal = yres + lower + vslen + upper;
	if (ytotal > 1024) {
                ytotal >>= 1;
                vslen >>= 1;
        } 
        if (ytotal > 1024)
                FAIL("ytotal too big");
        if (vslen > 16)
                FAIL("vslen too big");
	if (vxres >= 512)
                FAIL("vxres too long");
	return 0;	
}

int vga_set_mode(struct vga_hw_state *state, int double_scan) 
{
	int xtotal = state->xres + state->right + state->hslen + state->left;
	int ytotal = state->yres + state->lower + state->vslen + state->upper;
	u8 crtc[VGA_CRT_C];
	u8 gdc[VGA_GFX_C];
	u8 seq[VGA_SEQ_C];
	u8 atc[VGA_ATT_C];
	int pos, r7, fh, i;

	/*
	printk("xres is %d, vxres is %d, right is %d, hslen is %d, left is %d,
                yres is %d, lower is %d, vslen is %d, upper is %d, 
		xoffset is %d, yoffset is %d, palette_blank is %d, 
		vesa_blanked is %d, pel_mask is %d, clk_div is %d, 
		rMode is %d, misc is %d\n", 
                state->xres, state->vxres, state->right, state->hslen, state->left, 
		state->yres, state->lower, state->vslen, state->upper, state->xoffset,
		state->yoffset, state->palette_blanked, state->vesa_blanked,
		state->pel_mask, state->clkdiv,  
		state->rMode, state->misc);
	*/

        if (ytotal > 1024) {
                ytotal >>= 1;
                state->yres   >>= 1;
                state->lower  >>= 1;
                state->vslen  >>= 1;
                state->upper  >>= 1;
                state->rMode = 0x04;
        } else
                state->rMode = 0x00;
	
	crtc[VGA_CRTC_H_TOTAL] = xtotal - 5;
        crtc[VGA_CRTC_H_BLANK_START] = state->xres - 1;
        crtc[VGA_CRTC_H_DISP] = state->xres - 1;
        pos = state->xres + state->right;
        crtc[VGA_CRTC_H_SYNC_START] = pos;
        pos += state->hslen;
        crtc[VGA_CRTC_H_SYNC_END] = pos & 0x1F;
        pos += state->left - 2; /* blank_end + 2 <= total + 5 */
        crtc[VGA_CRTC_H_BLANK_END] = (pos & 0x1F) | 0x80;
        if (pos & 0x20)
                crtc[VGA_CRTC_H_SYNC_END] |= 0x80;

        crtc[VGA_CRTC_V_TOTAL] = ytotal - 2;
        r7 = 0x10;      /* disable linecompare */
        if (ytotal & 0x100) r7 |= 0x01;
        if (ytotal & 0x200) r7 |= 0x20;
        crtc[VGA_CRTC_PRESET_ROW] = 0;
        /* 1 scanline, no linecmp */
        crtc[VGA_CRTC_MAX_SCAN] = 0x40;
	if (double_scan)
                crtc[VGA_CRTC_MAX_SCAN] |= 0x80;
        crtc[VGA_CRTC_CURSOR_START] = 0x20;
        crtc[VGA_CRTC_CURSOR_END]   = 0x00;
        if ((state->mode & (MODE_CFB | MODE_8BPP)) == MODE_CFB)
                state->xoffset--;
        pos = state->yoffset * state->vxres + (state->xoffset >> state->shift);
        crtc[VGA_CRTC_START_HI]     = pos >> 8;
        crtc[VGA_CRTC_START_LO]     = pos & 0xFF;
        crtc[VGA_CRTC_CURSOR_HI]    = 0x00;
        crtc[VGA_CRTC_CURSOR_LO]    = 0x00;
        pos = state->yres - 1;
        crtc[VGA_CRTC_V_DISP_END] = pos & 0xFF;
        crtc[VGA_CRTC_V_BLANK_START] = pos & 0xFF;
        if (pos & 0x100)
                r7 |= 0x0A;     /* 0x02 -> DISP_END, 0x08 -> BLANK_START */
        if (pos & 0x200) {
                r7 |= 0x40;     /* 0x40 -> DISP_END */
                /* BLANK_START */
                crtc[VGA_CRTC_MAX_SCAN] |= 0x20;
        }
        pos += state->lower;
        crtc[VGA_CRTC_V_SYNC_START] = pos & 0xFF;
	if (pos & 0x100)
                r7 |= 0x04;
        if (pos & 0x200)
                r7 |= 0x80;
        pos += state->vslen;

        /* disabled IRQ */
        crtc[VGA_CRTC_V_SYNC_END] = (pos & 0x0F) & ~0x10;
        pos += state->upper - 1; /* blank_end + 1 <= ytotal + 2 */
        crtc[VGA_CRTC_V_BLANK_END] = pos & 0xFF; /* 0x7F for original VGA,
                     but some SVGA chips requires all 8 bits to set */
        crtc[VGA_CRTC_OFFSET] = state->vxres >> 1;
        if (state->mode & MODE_SKIP4)
                crtc[VGA_CRTC_UNDERLINE] = 0x5F;   /* 256, cfb8 */
        else
                crtc[VGA_CRTC_UNDERLINE] = 0x1F;   /* 16, vgap */
        crtc[VGA_CRTC_MODE] = state->rMode | ((state->mode & MODE_TEXT) ? 0xA3 : 0xE3);
        crtc[VGA_CRTC_LINE_COMPARE] = 0xFF;
        crtc[VGA_CRTC_OVERFLOW] = r7;

	seq[VGA_SEQ_CLOCK_MODE] = 0x01 | state->clkdiv;
        if (state->mode & MODE_TEXT)
                seq[VGA_SEQ_PLANE_WRITE] = 0x03;
        else
                seq[VGA_SEQ_PLANE_WRITE] = 0x0F;
        seq[VGA_SEQ_CHARACTER_MAP] = 0x00;
        if (state->mode & MODE_TEXT)
                seq[VGA_SEQ_MEMORY_MODE] = 0x03;
        else if (state->mode & MODE_SKIP4)
                seq[VGA_SEQ_MEMORY_MODE] = 0x0E;
        else
                seq[VGA_SEQ_MEMORY_MODE] = 0x06;

        gdc[VGA_GFX_SR_VALUE] = 0x00;
        gdc[VGA_GFX_SR_ENABLE] = 0x00;
        gdc[VGA_GFX_COMPARE_VALUE] = 0x00;
        gdc[VGA_GFX_DATA_ROTATE] = 0x00;
        gdc[VGA_GFX_PLANE_READ] = 0;
	if (state->mode & MODE_TEXT) {
                gdc[VGA_GFX_MODE] = 0x10;
                gdc[VGA_GFX_MISC] = 0x06;
        } else {
                if (state->mode & MODE_CFB)
                        gdc[VGA_GFX_MODE] = 0x40;
                else
                        gdc[VGA_GFX_MODE] = 0x00;
                gdc[VGA_GFX_MISC] = 0x05;
        }
        gdc[VGA_GFX_COMPARE_MASK] = 0x0F;
        gdc[VGA_GFX_BIT_MASK] = 0xFF;

        for (i = 0x00; i < 0x10; i++)
                atc[i] = i;
        if (state->mode & MODE_TEXT)
                atc[VGA_ATC_MODE] = 0x04;
        else if (state->mode & MODE_8BPP)
                atc[VGA_ATC_MODE] = 0x41;
        else
                atc[VGA_ATC_MODE] = 0x81;
        /* 0 for EGA, 0xFF for VGA */
        atc[VGA_ATC_OVERSCAN] = 0x00;
        atc[VGA_ATC_PLANE_ENABLE] = 0x0F;
	if (state->mode & MODE_8BPP)
                atc[VGA_ATC_PEL] = (state->xoffset & 3) << 1;
        else
                atc[VGA_ATC_PEL] = state->xoffset & 7;
        atc[VGA_ATC_COLOR_PAGE] = 0x00;

        if (state->mode & MODE_TEXT) {
		/* Font size register 
                fh = vga_rcrt(NULL, VGA_CRTC_MAX_SCAN); */
		fh = 16;
		crtc[VGA_CRTC_MAX_SCAN] = (crtc[VGA_CRTC_MAX_SCAN]
                                               & ~0x1F) | (fh - 1);
        }
        vga_w(NULL, VGA_MIS_W, vga_r(NULL, VGA_MIS_R) | 0x01);

        /* Enable graphics register modification */ 
        if (state->video_type == VIDEO_TYPE_EGAC) {
                vga_w(NULL, EGA_GFX_E0, 0x00);
                vga_w(NULL, EGA_GFX_E1, 0x01);
        }

        /* update misc output register */
        vga_w(NULL, VGA_MIS_W, state->misc);

        /* synchronous reset on */
        vga_wseq(NULL, VGA_SEQ_RESET, 0x01);

        if (state->video_type == VIDEO_TYPE_VGAC)
                vga_w(NULL, VGA_PEL_MSK, state->pel_mask);

        /* write sequencer registers */
        vga_wseq(NULL, VGA_SEQ_CLOCK_MODE, seq[VGA_SEQ_CLOCK_MODE] | 0x20);
        for (i = 2; i < VGA_SEQ_C; i++) {
                vga_wseq(NULL, i, seq[i]);
        }

        /* synchronous reset off */
        vga_wseq(NULL, VGA_SEQ_RESET, 0x03);

        /* deprotect CRT registers 0-7 */
        vga_wcrt(NULL, VGA_CRTC_V_SYNC_END, crtc[VGA_CRTC_V_SYNC_END]);

        /* write CRT registers */
        for (i = 0; i < VGA_CRTC_REGS; i++) {
                vga_wcrt(NULL, i, crtc[i]);
        }

        /* write graphics controller registers */
        for (i = 0; i < VGA_GFX_C; i++) {
                vga_wgfx(NULL, i, gdc[i]);
        }

        /* write attribute controller registers */
        for (i = 0; i < VGA_ATT_C; i++) {
                vga_r(NULL, VGA_IS1_RC);           /* reset flip-flop */
                vga_wattr(NULL, i, atc[i]);
        }

        /* Wait for screen to stabilize. */
        mdelay(50);

        vga_wseq(NULL, VGA_SEQ_CLOCK_MODE, seq[VGA_SEQ_CLOCK_MODE]);

        vga_r(NULL, VGA_IS1_RC);
        vga_w(NULL, VGA_ATT_IW, 0x20);
	return 0;
}

void vga_pal_blank(void)
{
	int i;
	
	for (i=0; i<16; i++) {
		vga_w(NULL, VGA_PEL_IW, i);
		vga_w(NULL, VGA_PEL_D, 0);
		vga_w(NULL, VGA_PEL_D, 0);
		vga_w(NULL, VGA_PEL_D, 0);
	}
}

void vga_vesa_blank(struct vga_hw_state *state, int mode) 
{
	/* Save the original values of VGA controller register */
	unsigned long flags;

	if (state->vesa_blanked) {
	 	spin_lock_irqsave(&vga_lock, flags);	
		state->SeqCtrlIndex = vga_r(NULL, VGA_SEQ_I);
		state->CrtCtrlIndex = vga_r(NULL, VGA_CRT_IC);	
	 	spin_unlock_irqrestore(&vga_lock, flags);	

		state->HorizontalTotal = vga_rcrt(NULL, 0x00); 
		state->HorizDisplayEnd = vga_rcrt(NULL, 0x01);
		state->StartHorizRetrace = vga_rcrt(NULL, 0x04);
		state->EndHorizRetrace = vga_rcrt(NULL, 0x05);
		state->Overflow = vga_rcrt(NULL, 0x07);
		state->StartVertRetrace = vga_rcrt(NULL, 0x10);
		state->EndVertRetrace = vga_rcrt(NULL, 0x11);
		state->ModeControl = vga_rcrt(NULL, 0x17);
		state->ClockingMode = vga_rseq(NULL, 0x01);
	}
	/* 
	 * assure that video is enabled 
	 * "0x20" is VIDEO_EBABLE bit in register 01 of sequencer 
	 */
	spin_lock_irqsave(&vga_lock, flags);	
	vga_wseq(NULL, VGA_SEQ_CLOCK_MODE, state->ClockingMode | 0x20);
	
	/* Test for vertical retrace in progress... */
	if ((state->CrtMiscIO & 0x80) == 0x80)
		vga_w(NULL, VGA_MIS_W, state->CrtMiscIO & 0xef);
	
	/*
	 * Set <End of vertical retrace> to minimum (0) and 
	 * <Start of vertical retrace> to maximum (incl. overflow)
	 * Result: turn off vertical sync (VSync) pulse.
	 */
	if (mode & VESA_VSYNC_SUSPEND) {
		/* StartVert Retrace - maximum value */
		vga_wcrt(NULL, VGA_CRTC_V_SYNC_START, 0xFF); 
		/* EndVertRetrace - minimum (bits 0..3) */
		vga_wcrt(NULL, VGA_CRTC_V_SYNC_END, 0x40); 
	        /* Overflow - bits 09,10 of vert. retrace */
		vga_wcrt(NULL, VGA_CRTC_OVERFLOW, state->Overflow | 0x84);
	}
	if (mode & VESA_HSYNC_SUSPEND) {
		/*
		 * Set <End of horizontal retrace> to minimum (0) and
		 * <Start of horizontal Retrace> to maximum
		 * Result: turn off horizontal sync (HSync) pulse.
		 */
		vga_wcrt(NULL, 0x04, 0xFF); /* StartHorizRetrace - maximum */
		vga_wcrt(NULL, 0x05, 0x00); /* EndHorizRetrace - minimum */
	}
	/* Restore both index registers */
	vga_w(NULL, VGA_SEQ_I, state->SeqCtrlIndex);
	vga_w(NULL, VGA_CRT_IC, state->CrtCtrlIndex);
	spin_unlock_irqrestore(&vga_lock, flags);			
}

void vga_vesa_unblank(struct vga_hw_state *state)
{
	/* restore original values of VGA controller registers */
	unsigned long flags;

	spin_lock_irqsave(&vga_lock, flags);	
	vga_w(NULL, VGA_MIS_W, state->CrtMiscIO);
		
	/* HorizontalTotal */
	vga_wcrt(NULL, 0x00, state->HorizontalTotal);
	/* HorizDisplayEnd */
	vga_wcrt(NULL, 0x01, state->HorizDisplayEnd);
	/* StartHorizRetrace */
	vga_wcrt(NULL, 0x04, state->StartHorizRetrace);
	/* EndHorizRetrace */
	vga_wcrt(NULL, 0x05, state->EndHorizRetrace);
	/* Overflow */
	vga_wcrt(NULL, 0x07, state->Overflow);
	/* StartVertRetrace */
	vga_wcrt(NULL, 0x10, state->StartVertRetrace);
	/* EndVertRetrace */
	vga_wcrt(NULL, 0x11, state->EndVertRetrace);
	/* ModeControl */
	vga_wcrt(NULL, 0x17, state->ModeControl);
	/* ClockingMode */
	vga_wseq(NULL, 0x01, state->ClockingMode);
	
	/* Restore index/control registers */
	vga_w(NULL, VGA_SEQ_I, state->SeqCtrlIndex);
	vga_w(NULL, VGA_CRT_IC, state->CrtCtrlIndex);
	spin_unlock_irqrestore(&vga_lock, flags);	
}

EXPORT_SYMBOL(vga_check_mode);
EXPORT_SYMBOL(vga_set_mode);
EXPORT_SYMBOL(vga_clock_chip);
EXPORT_SYMBOL(vga_pal_blank);
EXPORT_SYMBOL(vga_vesa_blank);
EXPORT_SYMBOL(vga_vesa_unblank);

/*
 * linux/include/linux/accels.h -- Functions to handle the accel engine for 
 *                                 the console system.
 *
 *   Copyright (c) 2000 James Simmons 
 *
 * This file defines the accel operations that are used for a accel 
 * engine console wrapper. This can be used for any cards that have 
 * hardware accelerated features. So this can be used with framebuffer
 * devices with accel functions or non framebuffer devices such as newport
 * graphics cards for SGI workstations. This is why this is not part of fb.h.
 * Framebuffer devices use fbcon-accel.c to use these functions. Non 
 * framebuffer cards would define their own *con.c file and use these 
 * functions as well. Also these functions can be used by other accel
 * orientated systems.  
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/* Graphics hardware state */
#define ACCEL_IDLE          0          /* has nothing to do            */
#define ACCEL_FILL          1          /* gets filled by application   */
#define ACCEL_EXEC          2          /* being executed               */
#define ACCEL_WAIT          3          /* wait for execution to finish */

#define ROP_COPY            0       /* For the BitBLT code */
#define ROP_XOR             1

    /*
     * Accelerated operations
     */

struct accel_ops {
     /* Setup the accel engine and allocate any DMA needed. */
     void (*engine_init)(void *par);
     /* Recovery for cards that lock up. */
     void (*engine_reset)(void *par);
     /* Tells us if the accel engine is idle */ 
     int (*engine_state)(void *par);
     /* swap the hardware state between VT and process switching */
     void (*context_switch)(void *old_par, void *new_par);
     /* Accel functions needed for the console system */
     void (*fillrect)(void *par, int x1, int y1, unsigned int width, 
                      unsigned int height, unsigned long color, int rop);  
     void (*copyarea)(void *par, int sx, int sy, unsigned int width, 
		      unsigned int height, int dx, int dy);
     void (*imageblit)(void *par, int dx, int dy, unsigned int width, 
                       unsigned int height, int image_depth, void *image);
};

struct accel_ops *gfxops[32];

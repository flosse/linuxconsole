/*
 * linux/include/video/skeletonfb.h -- example graphics header. See other
 *				       files in that directory. 
 *
 *  Copyright 2001 James Simmons (jsimmons@linux-fbdev.org)
 *
 * This files is designed to be independent of the fbdev layer. This header
 * is provided so userland applications can have a standard set of headers
 * to program graphics hardware with as well as other kernel subsystems
 * that need to program the video hardware. 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

struct xxx_par {
    /*
     *  The hardware specific data in this structure uniquely defines a
     *  graphics card pipe's state. What is a graphics pipe and a
     *  graphics pipeline ? The automotive assembly line is a classic
     *  example of a pipeline. Each car goes through a series of stages
     *  on its way to the exit gates. At any given time many cars could
     *  be in some stage of completion. Each stage is know as a pipe.
     *  Rendering can also function in the same way. For rendering each
     *  stage must be performed in a specific order but each stage itself
     *  can operate independent of the previous stage because each stage
     *  stage can operate on a different set of data sent to the graphics
     *  card. This allows several rendering operations to occur at the
     *  same time.
     *
     *  Todays hardware comes in a variety of setups. With this variety
     *  comes different ways pipes can exist. Most low end graphical
     *  cards lack a graphics pipeline. It can be thought of as a
     *  assembly line with only one car allowed on it at a time. Some
     *  hardware exist where the cards have more than one GPU (graphical
     *  processing unit). If each GPU can have different hardware states
     *  that are independent of each other then each CPU has a single
     *  pipe and they can work as a pipeline. Also their exist hardware
     *  where you can chain video cards together and feed them to one
     *  display device. Here each video card acts as a seperate pipe
     *  and we can achieve a pipeline effect. Be aware it might be
     *  possible to disable and enable individual pipes.
     */
};

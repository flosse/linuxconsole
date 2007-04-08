/** These lines are (stolen) borrowed from xf86Input.c
 */

#include <Xmd.h>
#include <XI.h>
#include <XIproto.h>
#include <xf86.h>
#include <Xpoll.h>
#include <xf86Priv.h>
#include <xf86_Config.h>
#include <xf86Xinput.h>
#include <xf86Procs.h>
#include <mi/mipointer.h>
#include <Xi/exglobals.h>

#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <linux/input.h>
#include "tuntitko-common.h"

#define DBG(_X,_Y)

#undef TUN_X_TIME
#define TUN_X_TIME(X)	GetTimeInMillis ()

LocalDevicePtr tun_switch_device = NULL;

static int
tunShareCorePointer(DeviceIntPtr	device)
{
  LocalDevicePtr	local = (LocalDevicePtr) device->public.devicePrivate;
    
  return((local->always_core_feedback &&
	  local->always_core_feedback->ctrl.integer_displayed));
}

void
tunPostMotionEvent(DeviceIntPtr	device)
{
  int				loop;
  xEvent			xE[2];
  deviceKeyButtonPointer	*xev  = (deviceKeyButtonPointer*) xE;
  deviceValuator		*xv   = (deviceValuator*) xev+1;

  LocalDevicePtr		local = (LocalDevicePtr) device->public.devicePrivate;
  TunDevicePtr			tun = (TunDevicePtr) local->private;  

  char				*buff = 0;
  Time				current;

  Bool				is_core = xf86IsCorePointer(device);
  Bool				is_shared = tunShareCorePointer(device);

#if 0
  int				*axisvals;
  AxisInfoPtr			axes;
#endif

  int				dx, dy;
  float				mult;

#if 0
  TLOG ("%d %d %d", tun->avaluators [0].value, tun->avaluators [1].value,
	tun->avaluators [2].value);
  TLOG ("Indices: %p %p %p", 
	tun->xval_to_lval_tbl [0], 
	tun->xval_to_lval_tbl [1], 
	tun->xval_to_lval_tbl [2]);
  TLOG ("Values: %d %d %d", 
	tun->xval_to_lval_tbl [0]->lid, 
	tun->xval_to_lval_tbl [1]->lid, 
	tun->xval_to_lval_tbl [2]->lid);  

  xf86PostMotionEvent (device, TRUE, 0, 3,
		       tun->avaluators [0].value, tun->avaluators [1].value,
		       tun->avaluators [2].value);
#endif

  TLOG( "%d xvaluators starting with %d", tun->nof_xvaluators, tun->first_avaluator );
  switch( tun->nof_xvaluators ) { 
  case 6: xf86PostMotionEvent (device, TRUE, 0, 6,
			       tun->xval_to_lval_tbl[0]->value, 
			       tun->xval_to_lval_tbl[1]->value, 
			       tun->xval_to_lval_tbl[2]->value, 
			       tun->xval_to_lval_tbl[3]->value, 
			       tun->xval_to_lval_tbl[4]->value, 
			       tun->xval_to_lval_tbl[5]->value ); return;
  case 5: xf86PostMotionEvent (device, TRUE, 0, 5,
			       tun->xval_to_lval_tbl[0]->value, 
			       tun->xval_to_lval_tbl[1]->value, 
			       tun->xval_to_lval_tbl[2]->value, 
			       tun->xval_to_lval_tbl[3]->value, 
			       tun->xval_to_lval_tbl[4]->value ); return;
  case 4: xf86PostMotionEvent (device, TRUE, 0, 4,
			       tun->xval_to_lval_tbl[0]->value, 
			       tun->xval_to_lval_tbl[1]->value, 
			       tun->xval_to_lval_tbl[2]->value, 
			       tun->xval_to_lval_tbl[3]->value ); return;
  case 3: xf86PostMotionEvent (device, TRUE, 0, 3,
			       tun->xval_to_lval_tbl[0]->value, 
			       tun->xval_to_lval_tbl[1]->value, 
			       tun->xval_to_lval_tbl[2]->value ); return;
  case 2: xf86PostMotionEvent (device, TRUE, 0, 2,
			       tun->xval_to_lval_tbl[0]->value, 
			       tun->xval_to_lval_tbl[1]->value ); return;
  case 1: xf86PostMotionEvent (device, TRUE, 0, 1,
			       tun->xval_to_lval_tbl[0]->value ); return;
  default: TLOG( "can't deal with this whole valuator situation.." );
  }
  
return;


  if (tun_switch_device && (is_core || is_shared)) {
    xf86SwitchCoreDevice(tun_switch_device, device);
  }
    
  /*  current = GetTimeInMillis(); */
  current = TUN_X_TIME (tun);
    
  if (!is_core) {
    if (HAS_MOTION_HISTORY(local)) {
      buff = ((char *)local->motion_history +
	      (sizeof(INT32) * local->dev->valuator->numAxes + sizeof(Time)) * local->last);
    }
  }

  for(loop=0; loop < tun->nof_xvaluators; loop++) 
    {
      switch (loop % 6) {
      case 0:
	xv->valuator0 = tun->xval_to_lval_tbl[loop % 6]->value;
	break;
      case 1:
	xv->valuator1 = tun->xval_to_lval_tbl[loop % 6]->value;

	DBG(5, ErrorF("xf86PostMotionEvent v0=%d v1=%d\n", xv->valuator0, xv->valuator1));
	    
	if (loop == 1 && !tun->is_absolute && device->ptrfeed && device->ptrfeed->ctrl.num) {
		/* modeled from xf86Events.c */
	  if (device->ptrfeed->ctrl.threshold) {
	    if ((abs(xv->valuator0) + abs(xv->valuator1)) >= device->ptrfeed->ctrl.threshold) {
	      xv->valuator0 = (xv->valuator0 * device->ptrfeed->ctrl.num) /
		device->ptrfeed->ctrl.den;
	      xv->valuator1 = (xv->valuator1 * device->ptrfeed->ctrl.num) /
		device->ptrfeed->ctrl.den;
	    }
	  }
	  else if (xv->valuator0 || xv->valuator1) {
	    dx = xv->valuator0;
	    dy = xv->valuator1;
	    mult = pow((float)(dx*dx+dy*dy),
		       ((float)(device->ptrfeed->ctrl.num) /
			(float)(device->ptrfeed->ctrl.den) - 1.0) / 
		       2.0) / 2.0;
	    DBG(6, ErrorF("mult=%f dxremaind=%f dyremaind=%f\n",
			  mult, local->dxremaind, local->dyremaind));
	    if (dx) {
	      local->dxremaind = mult * (float)dx + local->dxremaind;
	      xv->valuator0 = dx = (int)local->dxremaind;
	      local->dxremaind = local->dxremaind - (float)dx;
	    }
	    if (dy) {
	      local->dyremaind = mult * (float)dy + local->dyremaind;
	      xv->valuator1 = dy = (int)local->dyremaind;
	      local->dyremaind = local->dyremaind - (float)dy;
	    }
	  }
	  DBG(6, ErrorF("xf86PostMotionEvent acceleration v0=%d v1=%d\n", xv->valuator0, xv->valuator1));
	}
	break;
      case 2:
	xv->valuator2 = tun->xval_to_lval_tbl[loop % 6]->value;
	break;
      case 3:
	xv->valuator3 = tun->xval_to_lval_tbl[loop % 6]->value;
	break;
      case 4:
	xv->valuator4 = tun->xval_to_lval_tbl[loop % 6]->value;
	break;
      case 5:
	xv->valuator5 = tun->xval_to_lval_tbl[loop % 6]->value;
	break;
      }

      if ((loop % 6 == 5) || (loop == tun->nof_xvaluators - 1)) {
	xv->num_valuators = (loop % 6) + 1;
	xv->first_valuator = (loop / 6) * 6;
	    
	if (!is_core) {
	  xev->type = DeviceMotionNotify;
	  xev->detail = 0;
	  xf86Info.lastEventTime = xev->time = current;
	  xev->deviceid = device->id | MORE_EVENTS;
            
	  xv->type = DeviceValuator;
	  xv->deviceid = device->id;
	  
	  xv->device_state = 0;
		
	  if (HAS_MOTION_HISTORY(local)) {
	    *(Time*)buff = current;
	    memcpy(buff+sizeof(Time)+sizeof(INT32)*xv->first_valuator, &xv->valuator0,
		   sizeof(INT32)*xv->num_valuators);
	  }
		
	  xf86eqEnqueue(xE);
	}
	if ((is_core || is_shared) && (tun->nof_xvaluators >= 2)) {
	  int	x, y;

	  if ((*local->conversion_proc)(local,
					xv->first_valuator,
					xv->num_valuators,
					xv->valuator0,
					xv->valuator1,
					xv->valuator2,
					xv->valuator3,
					xv->valuator4,
					xv->valuator5,
					&x, &y) == FALSE) {
	    DBG(4, ErrorF("xf86PostMotionEvent conversion failed\n"));
	    continue;
	  }

	  DBG(4, ErrorF("xf86PostMotionEvent x=%d y=%d\n", x, y));

	  if (x == local->old_x && y == local->old_y) {
	    DBG(4, ErrorF("xf86PostMotionEvent same cursor position continuing\n"));
	    continue;
	  }
		
	  local->old_x = x;
	  local->old_y = y;
		
	  xf86Info.lastEventTime = current;

	  /* FL [Sat Jun 14 14:32:01 1997]
	   * needs to integrate with DGA and XTEST event posting
	   */

	  miPointerAbsoluteCursor(x, y, xf86Info.lastEventTime); 

	  if (!is_shared)
	    break;
	}
      }
    }	

  if (HAS_MOTION_HISTORY(local)) {
    local->last = (local->last + 1) % device->valuator->numMotionEvents;
    if (local->last == local->first)
      local->first = (local->first + 1) % device->valuator->numMotionEvents;
  }
}

void
tunPostProximityEvent (DeviceIntPtr device, int value)
{
  LocalDevicePtr		local = (LocalDevicePtr) device->public.devicePrivate;
  TunDevicePtr			tun = (TunDevicePtr) local->private;  

  int				loop;
  xEvent			xE[2];
  deviceKeyButtonPointer	*xev = (deviceKeyButtonPointer*) xE;
  deviceValuator		*xv = (deviceValuator*) xev+1;
  Bool				is_core = xf86IsCorePointer(device);

  TLOG ("Posilam proximku %d...", value);

  xf86PostProximityEvent (device, value, 0, 3,
		       tun->avaluators [0].value, tun->avaluators [1].value,
		       tun->avaluators [2].value);

return;


  if (is_core) {
    return;
  }
  

  xev->type = value ? ProximityIn : ProximityOut;
  xev->detail = 0;
  xev->deviceid = device->id | MORE_EVENTS;
	
  xv->type = DeviceValuator;
  xv->deviceid = device->id;
  xv->device_state = 0;

#if 0
  if ((device->valuator->mode & 1) == Relative) {
    num_valuators = 0;
  }
#endif  

  if (tun->nof_xvaluators != 0) {

    for(loop=0; loop < tun->nof_xvaluators; loop++) {
      switch (loop % 6) 
	{
	case 0:
	  xv->valuator0 = tun->xval_to_lval_tbl[loop % 6]->value;
	  break;
	case 1:
	  xv->valuator1 = tun->xval_to_lval_tbl[loop % 6]->value;
	  break;
	case 2:
	  xv->valuator2 = tun->xval_to_lval_tbl[loop % 6]->value;
	  break;
	case 3:
	  xv->valuator3 = tun->xval_to_lval_tbl[loop % 6]->value;
	  break;
	case 4:
	  xv->valuator4 = tun->xval_to_lval_tbl[loop % 6]->value;
	  break;
	case 5:
	  xv->valuator5 = tun->xval_to_lval_tbl[loop % 6]->value;
	  break;
	}
      if ((loop % 6 == 5) || (loop == tun->nof_xvaluators - 1)) {
	xf86Info.lastEventTime = xev->time = TUN_X_TIME (tun);

	xv->num_valuators = (loop % 6) + 1;
	xv->first_valuator = (loop / 6) * 6;
	
	xf86eqEnqueue(xE);
      }
    }
  }
  else {
    /* no valuator */
    xf86Info.lastEventTime = xev->time = TUN_X_TIME (tun);

    xv->num_valuators = 0;
    xv->first_valuator = 0;
      
    xf86eqEnqueue(xE);
  }
}

void
NOTblablaPostButtonEvent(DeviceIntPtr	device,
		    int			is_absolute,
		    int			button,
		    int			is_down,
		    int			first_valuator,
		    int			num_valuators,
		    ...)
{
    va_list			var;
    int				loop;
    xEvent			xE[2];
    deviceKeyButtonPointer	*xev	        = (deviceKeyButtonPointer*) xE;
    deviceValuator		*xv	        = (deviceValuator*) xev+1;
    ValuatorClassPtr		val		= device->valuator;
    Bool			is_core		= FALSE; // xf86IsCorePointer(device);
    Bool			is_shared       =  FALSE; // tunShareCorePointer(device);
    
    DBG(5, ErrorF("xf86PostButtonEvent BEGIN 0x%x(%s) button=%d down=%s is_core=%s is_shared=%s is_absolute=%s\n",
		  device, device->name, button,
		  is_down ? "True" : "False",
		  is_core ? "True" : "False",
		  is_shared ? "True" : "False",
		  is_absolute ? "True" : "False"));
    
    /* Check the core pointer button state not to send an inconsistent
     * event. This can happen with the AlwaysCore feature.
     */
    if ((is_core || is_shared) && !xf86CheckButton(button, is_down)) {
	return;
    }
    
    if (num_valuators && (!val || (first_valuator + num_valuators > val->numAxes))) {
	ErrorF("Bad valuators reported for device \"%s\"\n", device->name);
	return;
    }

    if (is_core || is_shared) {
      //	xf86SwitchCoreDevice(tun_switch_device, device);
    }

    if (!is_core) {
	xev->type = is_down ? DeviceButtonPress : DeviceButtonRelease;
	xev->detail = button;
	xev->deviceid = device->id | MORE_EVENTS;
	    
	xv->type = DeviceValuator;
	xv->deviceid = device->id;
	xv->device_state = 0;

	if (num_valuators != 0) {
	    int			*axisvals = val->axisVal;
	    
	    va_start(var, num_valuators);
      
	    for(loop=0; loop<num_valuators; loop++) {
		switch (loop % 6) {
		case 0:
		    xv->valuator0 = is_absolute ? va_arg(var, int) : axisvals[loop];
		    break;
		case 1:
		    xv->valuator1 = is_absolute ? va_arg(var, int) : axisvals[loop];
		    break;
		case 2:
		    xv->valuator2 = is_absolute ? va_arg(var, int) : axisvals[loop];
		    break;
		case 3:
		    xv->valuator3 = is_absolute ? va_arg(var, int) : axisvals[loop];
		    break;
		case 4:
		    xv->valuator4 = is_absolute ? va_arg(var, int) : axisvals[loop];
		    break;
		case 5:
		    xv->valuator5 = is_absolute ? va_arg(var, int) : axisvals[loop];
		    break;
		}
		if ((loop % 6 == 5) || (loop == num_valuators - 1)) {
		    xf86Info.lastEventTime = xev->time = GetTimeInMillis();
		    xv->num_valuators = (loop % 6) + 1;
		    xv->first_valuator = first_valuator + (loop / 6) * 6;
		    xf86eqEnqueue(xE);
		}
	    }
	    va_end(var);
	}
	else {
	    /* no valuator */
	    xf86Info.lastEventTime = xev->time = GetTimeInMillis();
	    xv->num_valuators = 0;
	    xv->first_valuator = 0;
	    xf86eqEnqueue(xE);
	}
    }

    if (is_core || is_shared) {
	/* core pointer */
	int       cx, cy;
      
	GetSpritePosition(&cx, &cy);
      
	xE->u.u.type = is_down ? ButtonPress : ButtonRelease;
	xE->u.u.detail =  device->button->map[button];
	xE->u.keyButtonPointer.rootY = cx;
	xE->u.keyButtonPointer.rootX = cy;
	xf86Info.lastEventTime = xE->u.keyButtonPointer.time = GetTimeInMillis();
	xf86eqEnqueue(xE);
    }
    DBG(5, ErrorF("xf86PostButtonEvent END\n"));
}


void
tunPostButtonEvent (DeviceIntPtr device, int button, int is_down)
{
  LocalDevicePtr		local = (LocalDevicePtr) device->public.devicePrivate;
  TunDevicePtr			tun = (TunDevicePtr) local->private;  


  int				loop;
  xEvent			xE[2];
  deviceKeyButtonPointer	*xev	        = (deviceKeyButtonPointer*) xE;
  deviceValuator		*xv	        = (deviceValuator*) xev+1;
  Bool				is_core		= xf86IsCorePointer(device);
  Bool				is_shared       = tunShareCorePointer(device);



  xf86PostButtonEvent (device, TRUE, button, is_down, 0, 3,
			 tun->xval_to_lval_tbl[0]->value,
			 tun->xval_to_lval_tbl[1]->value,
			 tun->xval_to_lval_tbl[2]->value);
  return;
#if 0
#endif

    
  /* Check the core pointer button state not to send an inconsistent
   * event. This can happen with the AlwaysCore feature.
   */
  if ((is_core || is_shared) && !xf86CheckButton(button, is_down)) {
    return;
  }

  if (is_core || is_shared) {
    xf86SwitchCoreDevice(tun_switch_device, device);
  }

  if (!is_core) {
    xev->type = is_down ? DeviceButtonPress : DeviceButtonRelease;
    xev->detail = button;
    xev->deviceid = device->id | MORE_EVENTS;
	    
    xv->type = DeviceValuator;
    xv->deviceid = device->id;
    xv->device_state = 0;

    if (tun->nof_xvaluators != 0) {
      for(loop=0; loop < tun->nof_xvaluators; loop++) {
	TLOG ("Looping: %d, val %d", loop,
	      tun->xval_to_lval_tbl[loop]->value);
	switch (loop % 6) {
	case 0:
	  xv->valuator0 = tun->xval_to_lval_tbl[loop]->value;
	  break;				    
	case 1:					    
	  xv->valuator1 = tun->xval_to_lval_tbl[loop]->value;
	  break;				    
	case 2:					    
	  xv->valuator2 = tun->xval_to_lval_tbl[loop]->value;
	  break;				    
	case 3:					    
	  xv->valuator3 = tun->xval_to_lval_tbl[loop]->value;
	  break;				    
	case 4:					    
	  xv->valuator4 = tun->xval_to_lval_tbl[loop]->value;
	  break;				    
	case 5:					    
	  xv->valuator5 = tun->xval_to_lval_tbl[loop]->value;
	  break;
	}
	if ((loop % 6 == 5) || (loop == tun->nof_xvaluators - 1)) {
	  xf86Info.lastEventTime = xev->time = GetTimeInMillis (); // TUN_X_TIME (tun);
	  xv->num_valuators = (loop % 6) + 1;
	  xv->first_valuator = (loop / 6) * 6;
	  xf86eqEnqueue(xE);
	  TLOG ("Queueing (loop %d) %d %d %d", loop,
		(int)xv->valuator0,(int)xv->valuator1,(int)xv->valuator2);
	}
      }
    }
    else {
      /* no valuator */
      xf86Info.lastEventTime = xev->time = GetTimeInMillis (); // TUN_X_TIME (tun);
      xv->num_valuators = 0;
      xv->first_valuator = 0;
      xf86eqEnqueue(xE);
    }
  }

  if (is_core || is_shared) {
    /* core pointer */
    int       cx, cy;
      
    GetSpritePosition(&cx, &cy);
      
    xE->u.u.type = is_down ? ButtonPress : ButtonRelease;
    xE->u.u.detail =  device->button->map[button];
    xE->u.keyButtonPointer.rootY = cx;
    xE->u.keyButtonPointer.rootX = cy;
    xf86Info.lastEventTime = xE->u.keyButtonPointer.time = GetTimeInMillis (); // TUN_X_TIME (tun);
    xf86eqEnqueue(xE);
  }
  DBG(5, ErrorF("xf86PostButtonEvent END\n"));

}


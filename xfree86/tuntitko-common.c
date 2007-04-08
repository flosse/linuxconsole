#include "tuntitko-common.h"

#include <linux/input.h>

#include <fcntl.h>
#include <unistd.h>

#include <extnsionst.h>
#include <extinit.h>
#include <exevents.h>

TLOG_VARIABLE;

static int
tunProcInit (DeviceIntPtr pTun)
{
  LocalDevicePtr	local = (LocalDevicePtr) pTun->public.devicePrivate;
  TunDevicePtr		tun = (TunDevicePtr) local -> private;
  TunDeviceIDInfo	device_ID;

  int			smin, swidth, sheight, smax;
  int			i, fd = -1;

  if ((fd = open (tun->input_device, O_RDONLY)) < 0)
    {
      ErrorF ("Tuntitko: unable to open file %s", tun->input_device);
      goto error;
    }

  tunQueryDeviceID (fd, device_ID);
  if (device_ID [ID_BUS] != tun->device_ID [ID_BUS] ||
      device_ID [ID_VENDOR] != tun->device_ID [ID_VENDOR] ||
      device_ID [ID_PRODUCT] != tun->device_ID [ID_PRODUCT] ||
      device_ID [ID_VERSION] != tun->device_ID [ID_VERSION])
    {
      ErrorF ("Tuntitko: something bad happend - device ID is not the same as configured device ID");
      goto error;
    }

  smin = 0;
  swidth = screenInfo.screens [0]->width;
  sheight = screenInfo.screens [0]->height;
  smax = (swidth < sheight) ? sheight : swidth;

  TLOG ("xvaluators %d", tun->nof_xvaluators);
  
  if (tun->nof_xvaluators)
    { /* register valuators */
      TunValuatorPtr		valptr;

      if (InitValuatorClassDeviceStruct (pTun, tun->nof_xvaluators, xf86GetMotionEvents,
					 local->history_size, tun->is_absolute) == FALSE)
	{
	  ErrorF("unable to allocate Valuator class device\n"); 
	  goto error;
	}

      for (i = 0; i < tun->nof_xvaluators; i++)
	if (tun->xval_to_lval_tbl [i] == NULL) /* register fake valuator */
	  InitValuatorAxisStruct (pTun, i, -1, 1, 1,0,1);
	else
	  {
	    valptr = tun->xval_to_lval_tbl [i];

	    if (valptr -> is_absolute == FALSE &&
		valptr->min >= valptr->max)
	      { 
		valptr->min = smin;
		if (i == 0) /* X */
		  valptr->max = swidth;
		else if (i == 1) /* Y */
		  valptr->max = sheight;
		else valptr->max = smax; 
	      }

	    valptr->value = (valptr->max + valptr->min) / 2;
	    InitValuatorAxisStruct (pTun, i, 
				    valptr->min,
				    valptr->max,
				    1,0,1 /* fake resolution */);
	  }

      /* allocate the motion history buffer if needed */
      xf86MotionHistoryAllocate (local);

      valptr = tun->xval_to_lval_tbl [0];
      if (valptr)
	tun->factorX = ((double) screenInfo.screens[0]->width)
	  / (double) (valptr->max - valptr->min);
      else tun->factorX = 1.0;

      valptr = (tun->nof_xvaluators > 1) 
	? tun->xval_to_lval_tbl [1]
	: NULL;

      if (valptr)
	tun->factorY = ((double) screenInfo.screens[0]->height)
	  / (double) (valptr->max - valptr->min);

      TLOG ("FX: %g, FY: %g", tun->factorX, tun->factorY);
    }

  if (tun->has_proximity)
    if (InitProximityClassDeviceStruct (pTun) == FALSE)
      {
	ErrorF ("unable to init proximity class device\n");
	goto error;
      }
  
  if (tun->max_xbutton)
    {
      CARD8		map [BUTTON_MAX];

      for (i = 0; i <= tun->max_xbutton; i++)
	map [i] = i;

      if (InitButtonClassDeviceStruct (pTun, tun->max_xbutton, map) == FALSE)
	{
	  ErrorF ("unable to init Button class device\n");
	  goto error;
	}
    }

#ifndef XFREE86_V4
  AssignTypeAndName (pTun, local->atom, local->name);
#endif

  close (fd);  /* let's hope that nobody change our device behind our back :*) */
  TLOG ("[%s] Init OK.", local->name);

  return Success;

 error:
  if (fd > 0)
    close (fd);

  return !Success;
}

static int
tunProc(DeviceIntPtr       pTun,
	int                what)
{
  LocalDevicePtr	local = (LocalDevicePtr) pTun->public.devicePrivate;
  TunDevicePtr		tun = (TunDevicePtr) local -> private;

  switch (what)
    {
    case DEVICE_INIT: 
      TLOG ("DEVICE_INIT (%s)", local -> name);
      return tunProcInit (pTun);

    case DEVICE_ON:
      TLOG ("DEVICE_ON (%s)", local -> name);

      if (local->fd < 0)
	if ((local->fd = open (tun->input_device, O_RDONLY)) == -1)
	  {
	    TLOG ("Can not open device %s file %s", 
		  local->name, tun->input_device);
	    return !Success;
	  }

#ifdef XFREE86_V4	    
      xf86AddEnabledDevice(local);
#else
      AddEnabledDevice(local->fd);
#endif
      pTun->public.on = TRUE;

      TLOG ("DEVICE_ON (%s): OK.", local -> name);      
      break;

    case DEVICE_OFF:
    case DEVICE_CLOSE:
      TLOG ("DEVICE_%s (%s)", 
	    what == DEVICE_OFF ? "OFF" : "CLOSE",
	    local -> name);
      if (local->fd >= 0)
	{
#ifdef XFREE86_V4	    
	  xf86RemoveEnabledDevice(local);
#else
	  RemoveEnabledDevice(local->fd);
#endif
	}
      close (local->fd);
      local->fd = -1;
      pTun->public.on = FALSE;
      break;

    default:
      ErrorF("unsupported mode=%d\n", what);
      return !Success;
    }

  return Success;
}

/** Read the new events from device and enqueue them.
 */

static void
tunReadInput (LocalDevicePtr	local)
{
  TunDevicePtr		tun = (TunDevicePtr) local -> private;
  TunValuatorPtr	valptr;
  struct input_event	ebuf [64], *ebufptr;
  int			rd, i;

  rd = read (local->fd, ebuf, sizeof (ebuf));
  if (rd < sizeof (struct input_event))
    {
      TLOG ("[%s] Error reading event device :(", local->name);
      return;
    }

  for (i = 0, ebufptr = ebuf; 
       i < rd / sizeof (struct input_event); 
       i++, ebufptr++)
    {
      TLOG ("[%s] Event %s ", local -> name, 
	    tunGetEventName (ebufptr->type));
      
      if (tun->xval_to_lval_tbl &&
	  (ebufptr->time.tv_sec - tun->last_event_time.tv_sec > 1 ||
	   ((ebufptr->time.tv_sec - tun->last_event_time.tv_sec) * 1000000 +
	    (ebufptr->time.tv_usec - tun->last_event_time.tv_usec)) > tun->delta_time))
	{
	  TLOG ("Pred postem");
	  tunPostMotionEvent (local->dev);
	  TLOG ("Po postu");
	  tun->last_event_time = ebufptr->time;
	}

      switch (ebufptr->type)
	{
	case EV_ABS:
	  if (ebufptr->code < tun->first_avaluator ||
	      ebufptr->code > (tun->first_avaluator + tun->nof_avaluators) ||
	      tun->avaluators [ebufptr->code - tun->first_avaluator].lid < 0)
	    {
	      ErrorF ("Unknown evaluator %d", ebufptr->code);
	      break;
	    }
	  valptr = tun->avaluators + (ebufptr->code - tun->first_avaluator);
	  
	  valptr->value = (valptr->upsidedown) 
	    ? (valptr->max - ebufptr->value + valptr->min)
	    : ebufptr->value;
	  break;

	case EV_KEY:
	  if (IS_BUTTON (ebufptr->code))
	    {
	      int		lbut = ebufptr->code - tun->first_lbutton;

	      if (lbut < 0 || lbut > tun->nof_lbuttons ||
		  (tun->lbut_to_xbut_tbl [lbut] == -1))
		{
		  ErrorF ("[%s] Unknown button %d (%s)", local->name, 
			  lbut, tunGetKeyName (lbut));
		  break;
		}

	      if (tun->lbut_to_xbut_tbl [lbut] == TUN_BUTTON_PROXIMITY)
		tunPostProximityEvent (local->dev, ebufptr->value);
	      else if (tun->lbut_to_xbut_tbl [lbut] > 0)
		tunPostButtonEvent (local->dev, tun->lbut_to_xbut_tbl [lbut], ebufptr->value);

	      break;
	    }
	default:
	  TLOG ("[%s] Unhandled event %d (%s)", 
		local->name, ebufptr->type, tunGetEventName (ebufptr->type));
	}
    }

#if 0
  TunDevicePtr		tun = (TunDevicePtr) local -> private;
  TunAbsValuatorPtr	absptr;
  struct input_event	ebuf [64], *ebufptr;
  int			rd, i, value;
  int			xdev;

  rd = read (local->fd, ebuf, sizeof (struct input_event) * 64);
  if (rd < sizeof (struct input_event))
    {
      TLOG ("[%s] Error reading :(", local->name);
      return;
    }
  
  for (i = 0; i < rd / sizeof (struct input_event); i++)
    {
      TLOG ("[%s] Event %s )", local -> name, 
	    tun_names_events [ebuf[i].type]? tun_names_events [ebuf[i].type] : "?");
      ebufptr = ebuf + i;


      switch (ebufptr -> type)
	{
	case EV_KEY:
	  if (IS_BUTTON (ebufptr->code))
	    {
	      int		lbutton;
	      int		button = -1;

	      lbutton = KEY_TO_BUTTON (ebufptr->code);
	      
	      if (lbutton < tun->nof_alloc_buttons)
		button = tun->buttons_trans_tbl [lbutton];

	      if (button < 0)
		break;

	      tunPostButtonEvent (local->dev, tun, button, ebufptr->value);
	    }
	  break;
	case EV_ABS:
	  xdev = tun->abs_trans_tbl [ebufptr->code];
	  if (xdev == -1)
	    break; /* ignored... */

	  absptr = tun->abs_valuators + xdev;
	  value = absptr->upsidedown 
	    ?  (absptr->max - ebufptr->value)
	    : ebufptr->value;
	    

	  if (tun->abs_bitmap [xdev] == 1) /* already got it... */
	    {
	      tunPostMotionEvent (local->dev, tun);
	      tun->abs_bitmap [xdev] = 1;
	      tun->nof_ev_abs = 1;
	      tun->abs_valuators [xdev].value = value;
	    }
	  else
	    {
	      tun->abs_valuators [xdev].value = value;
	      if (++(tun->nof_ev_abs) >= tun->nof_abs)
		tunPostMotionEvent (local->dev,tun);
	    }

	default:
	  break;
	}
    }
#endif
}

static void
tunClose (LocalDevicePtr	local)
{
  TLOG ("Close (%s)", local -> name);
}


/** ???.
 *  \todo tunChangeControl: Figure out what does it mean.
 */
static int
tunChangeControl(LocalDevicePtr local, xDeviceCtl *control)
{
  xDeviceResolutionCtl	*res;

  TLOG ("ChangeControl (%s)", local -> name);
  res = (xDeviceResolutionCtl *)control;
	
  if ((control->control != DEVICE_RESOLUTION) ||
      (res->num_valuators < 1))
    return (BadMatch);

  return(Success);
}

/** ???.
 */
int 
tunControlProc(LocalDevicePtr	local,
	       xDeviceCtl	*ctl)
{
  TLOG ("ControlProc (%s)", local -> name);
  return !Success;
}

/** ???.
 */
static int
tunSwitchMode(ClientPtr	client,
		  DeviceIntPtr	dev,
		  int		mode)
{
  LocalDevicePtr        local = (LocalDevicePtr)dev->public.devicePrivate;
  TLOG ("SwitchMode (%s)", local -> name);
  return !Success;
}


/** Convert valuators to X and Y.
 */

static Bool
tunConvert(LocalDevicePtr	local,
	   int		first,
	   int		num,
	   int		v0,
	   int		v1,
	   int		v2,
	   int		v3,
	   int		v4,
	   int		v5,
	   int*		x,
	   int*		y)
{
  TunDevicePtr tun = (TunDevicePtr) local->private;

  if (tun -> nof_xvaluators < 2)
    return FALSE;

#ifdef XFREE86_V4
  {
    TunValuatorPtr	valptr;

    valptr = tun->xval_to_lval_tbl [0];
    if (valptr)
      tun->factorX = ((double) screenInfo.screens[0]->width)
	/ (double) (valptr->max - valptr->min);
    else tun->factorX = 1.0;
  
    valptr = (tun->nof_xvaluators > 1) 
      ? tun->xval_to_lval_tbl [1]
      : NULL;
  
    if (valptr)
      tun->factorY = ((double) screenInfo.screens[0]->height)
	/ (double) (valptr->max - valptr->min);
    else
      tun->factorY = 1.0;
  }
#endif

  *x = v0 * tun->factorX;
  *y = v1 * tun->factorY;

  TLOG ("Convert (%s) (%d %d), NEW (%d %d)", local -> name, v0, v1, *x, *y);

  return TRUE;
}

/** Convert X and Y to valuators.
 */

static Bool
tunReverseConvert(LocalDevicePtr	local,
		  int		x,
		  int		y,
		  int		*valuators)
{
  TLOG ("ReverseConvert (%s)", local -> name);
  return FALSE;
}

LocalDevicePtr
tunAllocate (int id, char *name)
{
  LocalDevicePtr	local = (LocalDevicePtr) xalloc (sizeof (LocalDeviceRec));
  TunDevicePtr		priv = (TunDevicePtr) xalloc (sizeof (TunDeviceRec));

  TLOG ("Allocating linput%d (name %s)",  id, name);

  local->name = strdup (name);
  local->flags = XI86_NO_OPEN_ON_INIT;

  local->device_config = tunConfig; /* parsovatko */
  local->device_control = tunProc;   /* DEVICE_CLOSE apod */
  local->read_input = tunReadInput;  /* handluje cteni ze souboru */
  local->control_proc = tunChangeControl;
  local->close_proc = tunClose;      /* pozavira soubory */
  local->control_proc = tunControlProc; /* ??? */
  local->switch_mode = tunSwitchMode;  /* swica relativni a absolutni flazek... */
  local->conversion_proc = tunConvert; /* convert valuators to X and Y */
  local->reverse_conversion_proc = tunReverseConvert; /* convert valuators to X and Y */
  local->fd = -1;
  local->atom = 0;
  local->dev = NULL;

  local->private = priv; /* FIXME !!! */
  local->private_flags = 0;
  
  local->history_size = 0;
  local->old_x = -1;
  local->old_y = -1;

  priv->input_device = (char*) xalloc (TUN_DEFAULT_INPUT_PATH_LENGTH);
  sprintf (priv->input_device, TUN_DEFAULT_INPUT_PATH, id);

  priv->factorX = 1;
  priv->factorY = 1;

  priv->last_event_time.tv_sec = 0;
  priv->last_event_time.tv_usec = 0;

  priv->delta_time = 50;
  priv->last_valuator = -1;
  priv->num_of_recieved_valuators = 0;

  priv->nof_avaluators = 0;
  priv->first_avaluator = 0;
  priv->avaluators = NULL;

  priv->nof_rvaluators = 0;
  priv->first_rvaluator = 0;
  priv->rvaluators = NULL;

  priv->xval_to_lval_tbl = NULL;

  priv->nof_lbuttons = 0;
  priv->first_lbutton = 0;
  priv->lbut_to_xbut_tbl = NULL;

  priv->is_absolute = 1;
  priv->has_proximity = 0;
  priv->has_mouse_wheel_hack = 0;

  return local;
}

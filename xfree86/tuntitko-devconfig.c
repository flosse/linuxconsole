#include "tuntitko-common.h"

#include <string.h>
#include <stdarg.h>

int
tunQueryLinuxDriverVesrion (int fd, TunDeviceVersionInfo *info)
{
  int		version;

  memset (info, 0, sizeof (info));
  if ((ioctl (fd, EVIOCGVERSION, &version) != 0) ||
      (version == 0))
    return !Success;

  info->major = (version >> 16);
  info->major = (version >> 8) & 0xff;
  info->major = version & 0xff;
  
  return Success;
}

char*
tunQueryDeviceName (int fd)
{
  char		name [256] = "Unknown";
  
  ioctl (fd, EVIOCGNAME(sizeof(name)), name);
  return strdup (name);
}

void 
tunQueryDeviceID (int fd, TunDeviceIDInfo info)
{
  memset (info, 0, sizeof (TunDeviceIDInfo));
  ioctl (fd, EVIOCGID, info);
}

void
tunQueryDevice (int fd, TunDeviceInfo info)
{
  int		i;

  memset (info, 0, sizeof (TunDeviceInfo));
  ioctl (fd, EVIOCGBIT (0, EV_MAX), info [0]);

  for (i = 0; i < EV_MAX; i++)
    if (TEST_BIT (i, info [0]))
      ioctl (fd, EVIOCGBIT (i, KEY_MAX), info [i]);
}

void
tunQueryAbsValuator (int fd, int valuator, TunAbsValuatorInfo *info)
{
  ioctl (fd, EVIOCGABS (valuator), info);
}

void
tunDeviceRecForceXValuators (TunDevicePtr tun, int n)
{
  if (n >= tun->nof_xvaluators)
    {
      int		i;
      
      tun->xval_to_lval_tbl = tun->xval_to_lval_tbl
	? t_renew (TunValuatorPtr, tun->xval_to_lval_tbl, n)
	: t_new (TunValuatorPtr, n);
      
      for (i = tun->nof_xvaluators; i < n; i++)
	tun->xval_to_lval_tbl [i] = NULL;
      
      tun->nof_xvaluators = n;
    }
}

void
tunInitDeviceRec (int fd, LocalDevicePtr local, TunDevicePtr tun, TunDeviceInfo info)
{
  if (TUN_DEVICE_HAS_VAL_ABS (info) || TUN_DEVICE_HAS_VAL_REL (info))
    { /* valuators first ... */
      int		i,first_valuator = -1;
      int		last_valuator;
      int		nof_valuators = 0;
      TunValuatorPtr	val;
      TunAbsValuatorInfo aval_info;

      if (TUN_DEVICE_HAS_VAL_ABS (info))
	{ /* absolute valuators */
	  for (i = 0, first_valuator = -1; i < ABS_MAX; i++)
	    if (TUN_DEVICE_TEST_VAL_ABS (info, i))
	      {
		last_valuator = i;
		if (first_valuator == -1)
		  first_valuator = i;
		nof_valuators ++;

		TLOG ("[%s] Found absolute valuator %d (%s)", local->name,
		      i, tunGetAbsValuatorName (i));
	      }

	  tun->first_avaluator = first_valuator;
	  tun->nof_avaluators = last_valuator - first_valuator + 1;
	  tun->avaluators = t_new (TunValuatorRec, tun->nof_avaluators);

	  for (i = first_valuator; i <= last_valuator; i++)
	    {
	      val = tun->avaluators + (i - first_valuator);
	      val->xiv = -1;

	      if (TUN_DEVICE_TEST_VAL_ABS (info, i))
		{
		  val->lid = i;
		  tunQueryAbsValuator (fd, i, &aval_info);
		}
	      else
		val->lid = -1;

	      val->value = aval_info.value;
	      val->rel_value = aval_info.value;

	      val->min = aval_info.min;
	      val->max = aval_info.max;

	      TLOG ("Konfigurace valuatoru %d: (%d,%d,%d)",
		    i, val->value, val->min, val->max);

	      val->upsidedown = FALSE;
	      val->mouse_wheel_hack = FALSE;
	      val->abs_is_relspeed = FALSE;
	      val->is_absolute = TRUE;
	    }
	} /* absolute valuators */

      if (TUN_DEVICE_HAS_VAL_REL (info))
	{ /* relative valuators */
	  for (i = 0, first_valuator = -1; i < REL_MAX; i++)
	    if (TUN_DEVICE_TEST_VAL_REL (info, i))
	      {
		if (i > last_valuator) last_valuator = i;
		if (first_valuator == -1) first_valuator = i;
		nof_valuators++;

		TLOG ("[%s] Found relative valuator %d (%s)", local->name,
		      i, tunGetRelValuatorName (i));
	      }
	  
	  tun->first_rvaluator = first_valuator;
	  tun->nof_rvaluators = last_valuator - first_valuator + 1;
	  tun->rvaluators = t_new (TunValuatorRec, tun->nof_rvaluators);
	  
	  for (i = first_valuator; i <= last_valuator; i++)
	    {
	      val = tun->rvaluators + (i - first_valuator);
	      
	      if (TUN_DEVICE_TEST_VAL_REL (info, i))
		val->lid = i;
	      else
		val->lid = -1;

	      val->xiv = -1;

	      val->value = 0;
	      val->min = val->max = -1;

	      val->upsidedown = FALSE;
	      val->mouse_wheel_hack = FALSE;
	      val->abs_is_relspeed = FALSE;
	      val->is_absolute = FALSE;
	    }
	} /* relative valuators */

      tunDeviceRecForceXValuators (tun, nof_valuators);
    } /* valuators */

  TLOG ("Furt tady");

  if (TUN_DEVICE_HAS_KEYS (info))
    {
      int		nof_buttons, min_button, max_button;
      int		nof_keys, min_key, max_key;
      int		i;

      min_button = max_button = min_key = max_key = -1;
      nof_buttons = nof_keys = 0;

      for (i = 0; i < KEY_MAX; i++)
	if (TUN_DEVICE_TEST_KEY (info, i))
	  {
	    if (i < BTN_MISC)
	      { /* key */
		nof_keys ++;
		if (min_key == -1) min_key = i;
		max_key = i;
		TLOG ("[%s] Found key %d (%s)", local->name, i, 
		      tunGetKeyName (i));
	      }
	    else
	      { /* button */
		nof_buttons ++;
		if (min_button == -1) min_button = i;
		max_button = i;
		TLOG ("[%s] Found button %d (%s)", local->name, i,
		      tunGetKeyName (i)) ;
	      }
	  }

      if (nof_buttons)
	{
	  tun->nof_lbuttons = max_button - min_button + 1;
	  tun->first_lbutton = min_button;
	  
	  tun->lbut_to_xbut_tbl = t_new (short int, tun->nof_lbuttons);
	  for (i = 0; i < tun->nof_lbuttons; i++)
	    tun->lbut_to_xbut_tbl [i] = TUN_BUTTON_UNASSIGNED;
	}
    }
}

/************* Oraculum... ********************************************/

static void
tun_log_oraculum (LocalDevicePtr local, char *format, ...)
{
  va_list	args;
  
  fprintf (TLOG_FILE, TLOG_HEADER);
  fprintf (TLOG_FILE, "[Oraculum][%s]: ", local->name);
  va_start (args, format);
  vfprintf (TLOG_FILE, format, args);
  va_end (arg);
  fprintf (TLOG_FILE, "\n");
}

static void
tun_assign_button (LocalDevicePtr local, int id, int newid)
{
  tun_log_oraculum (local, "button %d (%s) is asigned to X button %d",
		    id, tunGetKeyName (id), newid);
}

static void
tun_assign_rel_valuator (LocalDevicePtr local, int id, int newid)
{
  tun_log_oraculum (local, "relative valuator %d (%s) is asigned to X valuator %d",
		    id, tunGetRelValuatorName (id), newid);
}

static void
tun_assign_abs_valuator (LocalDevicePtr local, int id, int newid)
{
  tun_log_oraculum (local, "absolute valuator %d (%s) is asigned to X valuator %d",
		    id, tunGetAbsValuatorName (id), newid);
}

static void
tun_assign_absrel_valuator (LocalDevicePtr local, int id, int newid)
{
  tun_log_oraculum (local, "absolute valuator %d (%s) is asigned to X relative valuator %d",
		    id, tunGetAbsValuatorName (id), newid);
}

#define OTLOG		tun_log_oraculum
#define BUTTON(L,X)  do { tun_assign_button (local, L,X);					  \
			 tun->lbut_to_xbut_tbl [L - tun->first_lbutton] = X; } while (0)
#define VREL(L,X)    do { tun_assign_rel_valuator (local, L,X);					  \
			  tun->rvaluators [L - tun->first_rvaluator] . xiv = X;			  \
			  tun->xval_to_lval_tbl [X] = tun->rvaluators + L - tun->first_rvaluator; \
			} while (0);
#define VABS(L,X)    do { tun_assign_abs_valuator (local, L,X);					  \
			  tun->avaluators [L - tun->first_avaluator] . xiv = X;			  \
			  tun->xval_to_lval_tbl [X] = tun->avaluators + L - tun->first_avaluator; \
			} while (0);
#define VABSASREL(L,X)    do { tun_assign_absrel_valuator (local, L,X);				  \
			  tun->avaluators [L - tun->first_avaluator].abs_is_relspeed = TRUE;	  \
			  tun->avaluators [L - tun->first_avaluator] . xiv = X;			  \
			  tun->xval_to_lval_tbl [X] = tun->avaluators + L - tun->first_avaluator; \
			} while (0);

#define BUTTON_TEST_AND_ASSIGN(L,X)								\
			 if (TUN_DEVICE_TEST_KEY (info, L)) BUTTON (L,X)
#define AVALUATOR(L)     tun->avaluators [L - tun->first_avaluator]
#define RVALUATOR(L)     tun->rvaluators [L - tun->first_rvaluator]


void
tunAutoconfigDeviceRec (LocalDevicePtr local, TunDevicePtr tun, TunDeviceInfo info)
{
  if (TUN_DEVICE_TEST_VAL_REL (info, REL_X) &&
      TUN_DEVICE_TEST_VAL_REL (info, REL_Y) &&
      TUN_DEVICE_TEST_KEY (info, BTN_LEFT))
    {
      OTLOG (local, "I found Rel(X,Y), Button(Left)");
      OTLOG (local, "I think it is a mouse! :)");
      
      VREL (REL_X, 0);
      VREL (REL_Y, 1);

      if (TUN_DEVICE_TEST_VAL_REL (info, REL_WHEEL))
	{
	  OTLOG (local, "I found mouse wheel - mapping to BUTTON 4 & 5");
	  RVALUATOR (REL_WHEEL). mouse_wheel_hack = TRUE;
	}
      BUTTON_TEST_AND_ASSIGN (BTN_LEFT, 1);
      BUTTON_TEST_AND_ASSIGN (BTN_RIGHT, 2);
      BUTTON_TEST_AND_ASSIGN (BTN_MIDDLE, 3);

      tun->is_absolute = FALSE;
      return;
    }

  if (TUN_DEVICE_TEST_VAL_ABS (info, ABS_X) &&
      TUN_DEVICE_TEST_VAL_ABS (info, ABS_Y) &&
      TUN_DEVICE_TEST_KEY (info, BTN_TOUCH))
    {
      OTLOG (local, "I found Abs (X,Y), Button(Touch)");
      OTLOG (local, "I think it is a Tablet! :)");

      VABS (ABS_X,0);	
      VABS (ABS_Y,1);

      TLOG ("Reverse Y coordinate (tablets have 0,0 in left lower corner)");
      AVALUATOR (ABS_Y).upsidedown = TRUE;
      
      if (TUN_DEVICE_TEST_VAL_ABS (info, ABS_PRESSURE))
	VABS (ABS_PRESSURE, 2);

      if (TUN_DEVICE_TEST_VAL_ABS (info, ABS_TILT_X))
	VABS (ABS_TILT_X, 3);

      if (TUN_DEVICE_TEST_VAL_ABS (info, ABS_TILT_Y))
	VABS (ABS_TILT_Y, 4);

      BUTTON (BTN_TOUCH, 1);
      BUTTON_TEST_AND_ASSIGN (BTN_STYLUS, 2);

      if (TUN_DEVICE_TEST_KEY (info, BTN_TOOL_PEN))
	{
	  OTLOG (local, "Proximity event using ToolPen button");
	  tun->lbut_to_xbut_tbl [BTN_TOOL_PEN - tun->first_lbutton] = TUN_BUTTON_PROXIMITY;
	}

      tun->is_absolute = TRUE;
      return;
    }

  if (TUN_DEVICE_TEST_VAL_ABS (info, ABS_X) &&
      TUN_DEVICE_TEST_VAL_ABS (info, ABS_Y) &&
      TUN_DEVICE_TEST_VAL_ABS (info, ABS_Z) &&
      TUN_DEVICE_TEST_VAL_ABS (info, ABS_RX) &&
      TUN_DEVICE_TEST_VAL_ABS (info, ABS_RY) &&
      TUN_DEVICE_TEST_VAL_ABS (info, ABS_RZ) &&
      AVALUATOR(ABS_X).min ==  - AVALUATOR(ABS_X).max &&
      AVALUATOR(ABS_Y).min ==  - AVALUATOR(ABS_Y).max &&
      AVALUATOR(ABS_Z).min ==  - AVALUATOR(ABS_Z).max &&
      AVALUATOR(ABS_RX).min == - AVALUATOR(ABS_RX).max &&
      AVALUATOR(ABS_RY).min == - AVALUATOR(ABS_RY).max &&
      AVALUATOR(ABS_RZ).min == - AVALUATOR(ABS_RZ).max)
    {
      OTLOG (local, "found Abs (X,Y,Z,RX,RY,RZ)");
      OTLOG (local, "I think it is some sort of 6DO device! :)");

      VABSASREL (ABS_X, 0);
      VABSASREL (ABS_Y, 1);
      VABSASREL (ABS_Z, 2);

      VABSASREL (ABS_RX, 3);
      VABSASREL (ABS_RY, 4);
      VABSASREL (ABS_RZ, 5);

      tun->is_absolute = FALSE;
      
      return;
    }
}


#define FTLOG	tunFinishLog

void
tunFinishLog (LocalDevicePtr local, char *format, ...)
{
  va_list	args;
  
  fprintf (TLOG_FILE, TLOG_HEADER);
  fprintf (TLOG_FILE, "[Finish][%s]: ", local->name);
  va_start (args, format);
  vfprintf (TLOG_FILE, format, args);
  va_end (arg);
  fprintf (TLOG_FILE, "\n");
}

void
tunFinishUnasigned (LocalDevicePtr local, TunDevicePtr tun, TunDeviceInfo info)
{
  int			i, j;
  TunValuatorPtr	vptr;

  if (tun->avaluators) 
    for (i = 0, vptr = tun->avaluators; i < tun->nof_avaluators; i++, vptr++)
      if (TUN_DEVICE_TEST_VAL_ABS (info, i + tun->first_avaluator) &&
	   (vptr->xiv == TUN_VALUATOR_UNASSIGNED))
	{
	  for (j = 0; j < tun->nof_xvaluators; j++)
	    if (tun->xval_to_lval_tbl [j] == NULL)
	      break;

	  if (j == tun->nof_xvaluators)
	    tunDeviceRecForceXValuators (tun, j);

	  tun->xval_to_lval_tbl [j] = vptr;
	  vptr->xiv = j;
	  
	  FTLOG (local, "Mapped absolute valuator %d (%s) to X valuator %d",
		 vptr->lid, tunGetAbsValuatorName (vptr->lid), j);
	}

  if (tun->rvaluators) 
    for (i = 0, vptr = tun->rvaluators; i < tun->nof_rvaluators; i++, vptr++)
      if (TUN_DEVICE_TEST_VAL_REL (info, i+tun->first_rvaluator) &&
	  (vptr->xiv == TUN_BUTTON_UNASSIGNED))
	{
	  for (j = 0; j < tun->nof_xvaluators; j++)
	    if (tun->xval_to_lval_tbl [j] == NULL)
	      break;

	  if (j == tun->nof_xvaluators)
	    tunDeviceRecForceXValuators (tun, j);

	  tun->xval_to_lval_tbl [j] = vptr;
	  vptr->xiv = j;

	  FTLOG (local, "Mapped absolute valuator %d (%s) to X valuator %d",
		 vptr->lid, tunGetRelValuatorName (vptr->lid), j);
	}
  
  if (tun->lbut_to_xbut_tbl)
    { 
      char		*bmap;
      int		 safe;
      short int		*trans;

      safe = tun->nof_lbuttons + 6;
      bmap = t_new (char, safe);
      memset (bmap, 0, safe);

      trans = tun->lbut_to_xbut_tbl;
      bmap [0] = TRUE;

      for (i = 0; i < tun->nof_lbuttons; i++)
	if ((0 < trans [i]) && (trans [i] < safe))
	  bmap [trans [i]] = TRUE;

      for (i = 0; i < tun->nof_rvaluators; i++)
	if (tun->rvaluators [i] . mouse_wheel_hack)
	  bmap [4] = bmap [5] = TRUE;

      for (i = 0; i < tun->nof_lbuttons; i++)
	if (TUN_DEVICE_TEST_KEY (info, i + tun->first_lbutton) &&
	    trans [i] == TUN_BUTTON_UNASSIGNED)
	  {
	    for (j = 0; j < safe; j++)
	      if (bmap [j] != TRUE) break;

	    trans [i] = j;
	    bmap [j] = TRUE;

	    FTLOG (local, "Assigned button %d (%s) to X button %d",
		   i+tun->first_lbutton, tunGetKeyName (i+tun->first_lbutton), j);
	  }
    }
}


void
tunDeviceRecFinalize (TunDevicePtr tun)
{
  static TunValuatorRec		fake;
  int				i;

  /* check for mouse_wheel_hack */

  for (i = 0; i < tun->nof_rvaluators; i++)
    if (tun->rvaluators [i].mouse_wheel_hack)
      { tun->has_mouse_wheel_hack = TRUE; }

  /* find bigest X button id and if we can send proximity event */
  tun->max_xbutton = 0;
  for (i = 0; i < tun->nof_lbuttons; i++)
    if (tun->lbut_to_xbut_tbl [i] == TUN_BUTTON_PROXIMITY)
      tun->has_proximity = TRUE;
    else if (tun->max_xbutton < tun->lbut_to_xbut_tbl [i])
      tun->max_xbutton = tun->lbut_to_xbut_tbl [i];

  if (tun->has_mouse_wheel_hack && tun->max_xbutton < 5)
    tun->max_xbutton = 5;

  fake.lid = -1;
  fake.xiv = -1;
  fake.value = 0;
  fake.rel_value = 0;
  fake.min = -1;
  fake.max = 1;

  fake.is_absolute = TRUE;
  fake.upsidedown = FALSE;
  fake.mouse_wheel_hack = FALSE;
  fake.abs_is_relspeed = FALSE;

  for (i = 0; i < tun->nof_xvaluators; i++)
    if (tun->xval_to_lval_tbl [i] == NULL)
      tun->xval_to_lval_tbl [i] = &fake;

#warning TODO: count number of working valuators (and use it in event heuristic)
}

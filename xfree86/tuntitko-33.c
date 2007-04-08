/** Tintitko - the X Input driver for Linux input drivers
 * XFree86 v 3.3 dependend code
 * (c) 2000 0rfelyus (Tuhyk-Labs)
 */

#include "tuntitko-common.h"
#include <linux/input.h>
#include "xf86Procs.h"
#include <fcntl.h> /* for `open' etc. */
#include <unistd.h> /* for `close' .. */

/**********************************************************************/

#define TUN_INPUT_REG(NUM)  \
    xf86AddDeviceAssoc (&linput ## NUM ## _assoc)
   
#define TUN_INPUT_GEN(NUM)					\
								\
static LocalDevicePtr						\
tunAllocate ## NUM ()						\
{								\
  return tunAllocate (NUM, "LInput" #NUM);			\
}								\
								\
DeviceAssocRec linput ## NUM ## _assoc =			\
{								\
  "linput" #NUM,		/* config_section_name */	\
  tunAllocate ## NUM		/* device_allocate */		\
}

/***********************************************************************/

#define ENDVALUATOR		 1
#define DEVMIN			 2
#define DEVMAX			 3
#define DEVICEMAP		 4
#define REVERSE			 5
#define MOUSEWHEELHACK		 6
#define DEVICENAME		 7
#define DEVICE			 8
#define NOAUTOCONFIG		 9
#define ABSOLUTE		10
#define TIMEDELTA		11
#define REL_VALUATOR		12
#define ABS_VALUATOR		13
#define BUTTON			14
#define DISABLE			15
#define DONT_FINISH_UNASIGNED	16


static SymTabRec ValuatorTab [] = {
  { ENDVALUATOR,	 	"endvaluator"},

  { DEVMIN,			"min"},
  { DEVMAX,			"max"},

  { DEVICEMAP,			"map" },
  { REVERSE,			"reverse" },
  { MOUSEWHEELHACK,		"mousewheelhack"},
  { DISABLE,			"disable"},
  { -1,				""},
};

static SymTabRec TunTab [] = {
  { ENDSUBSECTION,		"endsubsection"},
  { DONT_FINISH_UNASIGNED,	"dontfinishunasigned"},

  { DEVICENAME,			"devicename" },
  { DEVICE,			"device" },
  { NOAUTOCONFIG,		"noautoconfig"},
  { ABSOLUTE,			"absolute"},
  { TIMEDELTA,			"timedelta"},

  { REL_VALUATOR,		"relativevaluator"},
  { ABS_VALUATOR,		"absolutevaluator"},
  { BUTTON,			"button"},

  { -1,				"" }  /* * */
};

TUN_INPUT_GEN (0);
TUN_INPUT_GEN (1);
TUN_INPUT_GEN (2);
TUN_INPUT_GEN (3);
TUN_INPUT_GEN (4);
TUN_INPUT_GEN (5);
TUN_INPUT_GEN (6);
TUN_INPUT_GEN (7);

void
tunConfigAbsValuator (LocalDevicePtr	local,
		      TunDevicePtr	tun,
		      TunValuatorPtr	valuator,
		      LexPtr		val)
{
  int		token;

  TLOG ("[%s] Configuring valuator %d (%s)", local->name, 
	valuator->lid, tunGetAbsValuatorName (valuator->lid));

  while ((token = xf86GetToken (ValuatorTab)) != ENDVALUATOR)
    switch (token)
      {
      case DEVICEMAP:
	if (xf86GetToken (NULL) != NUMBER || val->num < 0)
	  xf86ConfigError ("Valuator number expected");

	if (valuator->xiv >= 0)
	  tun->xval_to_lval_tbl [valuator->xiv] = NULL;	  

	tunDeviceRecForceXValuators (tun, val->num);

	if (tun->xval_to_lval_tbl [val->num] != NULL)
	  tun->xval_to_lval_tbl [val->num] -> xiv = TUN_VALUATOR_UNASSIGNED;

	tun->xval_to_lval_tbl [val->num] = valuator;
	valuator->xiv = val->num;
	break;

      case REVERSE:
	if (xf86GetToken (NULL) != NUMBER)
	  xf86ConfigError ("0 or 1 expected");
	valuator->upsidedown = (val->num != 0);
	break;

      case DISABLE:
	if (valuator->xiv >= 0)
	  tun->xval_to_lval_tbl [valuator->xiv] = NULL;
	valuator->xiv = TUN_VALUATOR_DISABLED;
	break;

      case DEVMIN:
      case DEVMAX:
      case MOUSEWHEELHACK:
	xf86ConfigError ("Only relative valuators can use this option");
	break;

      case EOF:
	FatalError("Unexpected EOF (missing EndSubSection)");
	break; /* :-) */
      default:
	xf86ConfigError("LInput valuator subsection keyword expected");
	break;
      }
}


void
tunConfigRelValuator (LocalDevicePtr	local,
		      TunDevicePtr	tun,
		      TunValuatorPtr	valuator,
		      LexPtr		val)
{
  int		token;

  TLOG ("[%s] Configuring valuator %d (%s)", local->name, 
	valuator->lid, tunGetRelValuatorName (valuator->lid));

  while ((token = xf86GetToken (ValuatorTab)) != ENDVALUATOR)
    switch (token)
      {
      case DEVICEMAP:
	if (xf86GetToken (NULL) != NUMBER || val->num < 0)
	  xf86ConfigError ("Valuator number expected");

	if (valuator->xiv >= 0)
	  tun->xval_to_lval_tbl [valuator->xiv] = NULL;	  

	tunDeviceRecForceXValuators (tun, val->num);

	if (tun->xval_to_lval_tbl [val->num] != NULL)
	  tun->xval_to_lval_tbl [val->num] -> xiv = TUN_VALUATOR_UNASSIGNED;

	tun->xval_to_lval_tbl [val->num] = valuator;
	valuator->xiv = val->num;
	break;

      case REVERSE:
	if (xf86GetToken (NULL) != NUMBER)
	  xf86ConfigError ("0 or 1 expected");
	valuator->upsidedown = (val->num != 0);
	break;

      case DISABLE:
	if (valuator->xiv >= 0)
	  tun->xval_to_lval_tbl [valuator->xiv] = NULL;
	valuator->xiv = TUN_VALUATOR_DISABLED;
	break;

      case DEVMIN:
	if (xf86GetToken (NULL) != NUMBER)
	  xf86ConfigError ("Number expected");

	valuator->min = val->num;
	break;

      case DEVMAX:
	if (xf86GetToken (NULL) != NUMBER)
	  xf86ConfigError ("Number expected");

	valuator->max = val->num;
	break;

      case MOUSEWHEELHACK:
	if (xf86GetToken (NULL) != NUMBER)
	  xf86ConfigError ("0 or 1 expected");

	valuator->mouse_wheel_hack = (val->num != 0);
	break;

      case EOF:
	FatalError("Unexpected EOF (missing EndSubSection)");
	break; /* :-) */
      default:
	xf86ConfigError("LInput valuator subsection keyword expected");
	break;
      }
}

/** Parse SubSection of XF86Config file
 */
Bool
tunConfig (LocalDevicePtr	*array,
	   int			index,
	   int			max,
	   LexPtr		val)
{
  LocalDevicePtr	local = array [index];
  TunDevicePtr		tun = (TunDevicePtr) local -> private;
  int			token, fd, i, finish = TRUE;

  TunDeviceVersionInfo	version_info;
  TunDeviceInfo		device_info;
  
  if (tun_switch_device == NULL)
    for  (i = 0; i < max; i++)
      if (strcmp (array [i]->name, "SWITCH") == 0)
	{ tun_switch_device = array [i]; }

  if (! tun_switch_device)
    TLOG ("Can not found SWITCH device :(");

  TLOG ("Config of device %s", local  -> name);

  if ((token = xf86GetToken (TunTab)) == DEVICE)
    {
      if (xf86GetToken (NULL) != STRING)
	xf86ConfigError ("String expected");

      t_free (tun->input_device);
      tun->input_device = strdup (val->str);
      token = xf86GetToken (TunTab);
    }

  if ((fd = open (tun->input_device, O_RDONLY)) == 0)
    {
      ErrorF ("Can not open %s file", tun->input_device);
      xf86ConfigError ("Can not open linux input device file");
    }

  if (tunQueryLinuxDriverVesrion (fd, &version_info) != Success)
    xf86ConfigError ("Bad Linux Input driver version or IOCTL interface does not work :(");

  tunQueryDeviceID (fd, tun->device_ID);

  { 
    char *name = tunQueryDeviceName (fd);
    TLOG ("[%s]: %s", local->name, name);
    t_free (name);
  }

  tunQueryDevice (fd, device_info);
  tunInitDeviceRec (fd, local, tun, device_info);

  TLOG ("TOKEN: %d", token);
  if (token != NOAUTOCONFIG)
    tunAutoconfigDeviceRec (local, tun, device_info);
  else
    token = xf86GetToken (TunTab);

  for ( /* NOP ! (WOW :) */ ; 
	(token != ENDSUBSECTION) ;
	token = xf86GetToken (TunTab))
    switch (token)
      {
      case DEVICENAME:
	if (xf86GetToken (NULL) != STRING)
	  xf86ConfigError ("Option string expected");
	TLOG ("LInput X device name changed from %s to %s",  local->name, val->str);
	xfree (local->name);
	local->name = strdup (val->str);
	break;	    

      case DEVICE:
      case NOAUTOCONFIG:
	xf86ConfigError ("This option is only allowed at the beginning of this section");
	break;

      case ABSOLUTE:
	if (xf86GetToken (NULL) != NUMBER)
	  xf86ConfigError ("Expected 0 or positive integer.");

	tun->is_absolute = (val->num != 0);
	break;

      case TIMEDELTA:
	if (xf86GetToken (NULL) != NUMBER)
	  xf86ConfigError ("Number expected.");

	tun->delta_time = val->num;
	break;

      case ABS_VALUATOR:
	{
	  int	lid = -1;
	  switch (xf86GetToken (NULL))
	    {
	    case NUMBER: lid = val->num; break;
	    case STRING: lid = tunLookUpAbsValuator (val->str); break;
	    }

	  if (lid == -1)
	    xf86ConfigError ("Abs valuator name or id expected");

	  if (TUN_DEVICE_TEST_VAL_ABS (device_info, lid))
	    tunConfigAbsValuator (local, tun, 
				  tun->avaluators + (lid - tun->first_avaluator),
				  val);
	  else
	    TLOG ("[%s] device does not have valuator %d (%s)",
		  local->name, lid, tunGetAbsValuatorName (lid)) ;

	  break;
	}
      case REL_VALUATOR:
	{
	  int	lid = -1;
	  switch (xf86GetToken (NULL))
	    {
	    case NUMBER: lid = val->num; break;
	    case STRING: lid = tunLookUpRelValuator (val->str); break;
	    }

	  if (lid == -1)
	    xf86ConfigError ("Abs valuator name or id expected");


	  if (TUN_DEVICE_TEST_VAL_REL (device_info, lid))
	    tunConfigRelValuator (local, tun, 
				  tun->rvaluators + (lid - tun->first_rvaluator),
				  val);
	  else
	    TLOG ("[%s] device does not have valuator %d (%s)",
		  local->name, lid, tunGetRelValuatorName (lid)) ;
	  break;
	}

      case BUTTON:
	{
	  int but = -5, lbut = -1;

	  switch (xf86GetToken (NULL))
	    {
	    case STRING:
	      lbut = KEY_TO_BUTTON (tunLookUpKey (val->str));
	      break;
	    case NUMBER:
	      lbut = val->num;
	    }

	  if (lbut < 0)
	    xf86ConfigError ("Button identifier expected");
	  
	  switch (xf86GetToken (NULL))
	    {
	    case NUMBER: 
	      if (val->num > 0)
		but = val->num; 
	      break;
	    case STRING: 
	      if (StrCaseCmp (val->str, "disabled") == 0) 
		 but = TUN_BUTTON_UNASSIGNED;
	      else if (StrCaseCmp (val->str, "proximity") == 0)
		but = TUN_BUTTON_PROXIMITY;
	      break ;
	    }

	  if (TUN_DEVICE_TEST_KEY (device_info, lbut) == 0)
	    {
	      TLOG ("Warning: button %s not found in device %s. IGNORED", 
		    tunGetKeyName (lbut), local->name);
	      break;
	    }

	  if (but == -5)
	    xf86ConfigError ("Expected positive integer, \"Disabled\" or \"Proximity\"");

	  tun->lbut_to_xbut_tbl [lbut - tun->first_lbutton] = but;
	  break;
	}

      case DONT_FINISH_UNASIGNED:
	finish = FALSE;
	break;

      case EOF:
	FatalError("Unexpected EOF (missing EndSubSection)");
	break; /* :-) */
      default:
	xf86ConfigError("LInput subsection keyword expected");
	break;
      }

  if (finish)
    tunFinishUnasigned (local, tun, device_info);

  tunDeviceRecFinalize (tun);

  close (fd);
  if (xf86Verbose) 
    ErrorF("%s %s: Configured.\n", XCONFIG_GIVEN, local->name);

  return Success;
}

/** Init module on X server start.
 */

int
init_module (unsigned long server_version)
{
  TLOG_INITCODE;

  fprintf (TLOG_FILE, 
  "******************************************************************************************");
  TLOG ("Tuntitko 0.1 - Linux Input Driver... Loaded");

  TUN_INPUT_REG (0);
  TUN_INPUT_REG (1);
  TUN_INPUT_REG (2);
  TUN_INPUT_REG (3);
  TUN_INPUT_REG (4);
  TUN_INPUT_REG (5);
  TUN_INPUT_REG (6);
  TUN_INPUT_REG (7);

  if (server_version != XF86_VERSION_CURRENT)
    {	
	ErrorF ("Warning: Tuntitko compiled for version %s\n", XF86_VERSION);
	return 0;
    }

  return 1;
}

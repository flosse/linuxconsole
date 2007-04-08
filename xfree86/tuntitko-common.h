#ifndef __TUNTITKO_COMMON_H__
#define __TUNTITKO_COMMON_H__

#include <stdio.h>

#include <xf86.h>
#include <xf86Version.h>
#include <xf86_Config.h>
#include <xf86Xinput.h>

#include <linux/input.h>

/************* Config options *****************************************/

#define TUN_DEFAULT_INPUT_PATH		"/dev/input/event%d"
#define TUN_DEFAULT_INPUT_PATH_LENGTH	24

/************* Debug macros *******************************************/

#define TLOG_HEADER		"Tuntitko: "

#ifndef ORFLOG
# define TLOG_FILE		stderr
# define TLOG_INITCODE	
# define TLOG_VARIABLE		extern FILE* logfile
# define TLOG_FLUSH
#else
# define TLOG_FILE		tlogfile
# define TLOG_INITCODE		do { tlogfile = fopen ("/home/0rfelyus/prace/tuntitko/tuntitko-log", "a");	\
				 if (tlogfile == NULL)								\
				   { tlogfile = stderr;								\
				    fprintf (stderr, TLOG_HEADER "can not open 0rf's logfile") ;			\
			         }										\
				 else										\
				   setlinebuf (tlogfile);							\
				} while (0)
# define TLOG_VARIABLE		FILE *tlogfile
# define TLOG_FLUSH		fflush (tlogfile)
extern FILE* logfile;
#endif

#ifdef __GNUC__
# define TLOG(format, args...)	 \
  do { fprintf (TLOG_FILE, TLOG_HEADER); fprintf (TLOG_FILE, format, ##args); fprintf (TLOG_FILE, "\n"); } while (0)
#else

# include <stdarg.h>
  static void
  TLOG (char *format, ...)
  {
    va_list args;
    va_start (args, format);
    fprintf (TLOG_FILE, TLOG_HEADER);
    vfprintf (TLOG_FILE, format, args);
    fprintf (TLOG_FILE, "\n");
    va_end (args);
  }
#endif
  

/************* Convenience macros *************************************/

#define t_new(TYPE,N)		(TYPE *) xalloc (sizeof (TYPE) * N)
#define t_renew(TYPE,MEM,N)	(TYPE *) xrealloc (MEM, sizeof(TYPE) * N)
#define t_free			xfree

/************* Query device features **********************************/

#define BITS_PER_LONG		(sizeof (long) * 8)
#define NBITS(x)		((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)			((x)%BITS_PER_LONG)
#define LONG(x)			((x)/BITS_PER_LONG)
#define TEST_BIT(bit, array)	((array[LONG(bit)] >> OFF(bit)) & 1)

#define TUN_DEVICE_HAS_VAL_ABS(INFO)   (TEST_BIT (EV_ABS, INFO [0]))
#define TUN_DEVICE_HAS_VAL_REL(INFO)   (TEST_BIT (EV_REL, INFO [0]))
#define TUN_DEVICE_HAS_KEYS(INFO)      (TEST_BIT(EV_KEY, INFO [0]))


#define TUN_DEVICE_TEST_VAL_ABS(INFO, VAL)	(TEST_BIT(VAL, INFO [EV_ABS]))
#define TUN_DEVICE_TEST_VAL_REL(INFO, VAL)	(TEST_BIT(VAL, INFO [EV_REL]))
#define TUN_DEVICE_TEST_KEY(INFO, KEY)		(TEST_BIT(KEY, INFO [EV_KEY]))

typedef unsigned long		TunDeviceInfo [EV_MAX][NBITS(KEY_MAX)];
typedef struct { int major, minor, micro; } TunDeviceVersionInfo;
typedef unsigned short TunDeviceIDInfo [4];
typedef struct {
  int		value;
  int		min;
  int		max;
  int		fuzz;
  int		flat;
} TunAbsValuatorInfo;


int 		tunQueryLinuxDriverVesrion (int fd, TunDeviceVersionInfo *info);
char*		tunQueryDeviceName (int fd);
void		tunQueryDevice (int fd, TunDeviceInfo info);
void		tunQueryAbsValuator (int fd, int valuator, TunAbsValuatorInfo *info);
void		tunQueryDeviceID (int fd, TunDeviceIDInfo info);

/**********************************************************************/

typedef struct _TunValuatorRec {
  short int	lid;		/* id of valuator as Linux Input valuator */
  short int	xiv;		/* X input valuator index */

  int		value;		/* absolute value */
  int		rel_value;	/* relative value */

  int		min;		/* min & max for relative  valuators used as absolute... */
  int		max;

  /* flags */
  unsigned int		is_absolute : 1;

  /* revert up and down */
  unsigned int		upsidedown : 1;		

  /* convert relative valuator to BUTTON4 & BUTTON5 buttons */ 
  unsigned int		mouse_wheel_hack : 1;

 /* there are some strenge joysticks, which report absolute valuator -
  * slope of stick which should be interpreted as acceleration of
  * relative valuator... complicated, huh?
  */
  unsigned int		abs_is_relspeed : 1;
} TunValuatorRec, *TunValuatorPtr;

typedef struct _TunDevRec {
  TunDeviceIDInfo	device_ID;

  char			*input_device;
  double		factorX, factorY;

  struct timeval	last_event_time;
  /** variables for event heuristics ... */
  int			delta_time;
  int			last_valuator;
  int			num_of_recieved_valuators;
  /* end of heuristic */

  int			nof_avaluators;
  int			first_avaluator;
  TunValuatorPtr	avaluators;

  int			nof_rvaluators;
  int			first_rvaluator;
  TunValuatorPtr	rvaluators;

  int			nof_xvaluators;
  TunValuatorPtr       *xval_to_lval_tbl;

  int			nof_lbuttons;
  int			first_lbutton;
  short int	       *lbut_to_xbut_tbl;
  int			max_xbutton;

  /* whether we report absolute/relative coordinates */
  unsigned int		is_absolute : 1;	
  /* whether we can send proximity evens... */
  unsigned int		has_proximity : 1;
  /* whether we have some relative valuator with mouse_wheel_hack */
  unsigned int		has_mouse_wheel_hack : 1;
} TunDeviceRec, *TunDevicePtr;


void	tunInitDeviceRec (int fd, LocalDevicePtr local, TunDevicePtr tun, TunDeviceInfo info);
void	tunAutoconfigDeviceRec (LocalDevicePtr, TunDevicePtr, TunDeviceInfo);
void	tunDeviceRecForceXValuators (TunDevicePtr tun, int n);
void	tunFinishUnasigned (LocalDevicePtr local, TunDevicePtr tun, TunDeviceInfo info);
void    tunDeviceRecFinalize (TunDevicePtr tun);

#define TUN_X_TIME(TUN)		(TUN->last_event_time.tv_sec * 1000 + \
				 TUN->last_event_time.tv_usec / 1000)

/**********************************************************************/

extern FILE* TLOG_FILE;

LocalDevicePtr tunAllocate (int, char*);
void	       tunAllocateButtons (TunDevicePtr, int last);

int		tunLookUpKey (char *string);
int		tunLookUpAbsValuator (char *string);
int		tunLookUpRelValuator (char *string);

char*		tunGetKeyName (int id);
char*		tunGetAbsValuatorName (int id);
char*		tunGetRelValuatorName (int id);
char*		tunGetEventName (int id);

/***********************************************************************/

Bool tunConfig (LocalDevicePtr	*array,
		int		index,
		int		max,
		LexPtr		val);

/**********************************************************************/

void		tunPostMotionEvent (DeviceIntPtr device);
void		tunPostProximityEvent (DeviceIntPtr device, int value);
void		tunPostButtonEvent (DeviceIntPtr device, int button, int is_down);

/************* Names ... **********************************************/

#define TUN_BUTTON_UNASSIGNED		-1
#define TUN_BUTTON_PROXIMITY		-2
#define TUN_BUTTON_DISABLED		-3

#define TUN_VALUATOR_UNASSIGNED		-1
#define TUN_VALUATOR_DISABLED		-2

#define BUTTON_MAX		(KEY_MAX - BTN_MISC)
#define KEY_TO_BUTTON(X)	((X) - BTN_MISC)
#define BUTTON_TO_KEY(X)	((X) + BTN_MISC)

#define IS_KEY(X)		((0 < (X)) && ((X) < BTN_MISC))
#define IS_BUTTON(X)		((BTN_MISC <= (X)) && ((X) < KEY_MAX))

/** Names of absolute valuators.
 */
extern char *tun_names_abs [];
extern char *tun_names_events [];
extern char *tun_names_rel [];
extern char *tun_names_keys [];

/** ugly hack */
extern LocalDevicePtr	tun_switch_device;

#endif

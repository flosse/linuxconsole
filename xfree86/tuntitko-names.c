
/** stolen from evtest example.
 */

#include "tuntitko-common.h"
#include <xf86_OSproc.h> /* for StrCaseCmp... */

char *tun_names_abs [ABS_MAX + 1] = {
  "X", "Y", "Z", "Rx", "Ry", "Rz", "Throttle", "Rudder", "Wheel", "Gas", "Brake",
  NULL, NULL, NULL, NULL, NULL,
  "Hat0X", "Hat0Y", "Hat1X", "Hat1Y", "Hat2X", "Hat2Y", "Hat3X", "Hat3Y", "Pressure", "Distance", "XTilt", "YTilt"
};

char *tun_names_rel [REL_MAX + 1] = {
  "X", "Y", "Z", NULL, NULL, NULL, "HWheel", "Dial", "Wheel" 
};

char *tun_names_events [EV_MAX + 1] = { 
  "Reset", "Key", "Relative", "Absolute", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, "LED", "Sound", NULL, "Repeat" 
};

char *tun_names_keys [] = { "Reserved", "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "Minus", "Equal", "Backspace",
"Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "LeftBrace", "RightBrace", "Enter", "LeftControl", "A", "S", "D", "F", "G",
"H", "J", "K", "L", "Semicolon", "Apostrophe", "Grave", "LeftShift", "BackSlash", "Z", "X", "C", "V", "B", "N", "M", "Comma", "Dot",
"Slash", "RightShift", "KPAsterisk", "LeftAlt", "Space", "CapsLock", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
"NumLock", "ScrollLock", "KP7", "KP8", "KP9", "KPMinus", "KP4", "KP5", "KP6", "KPPlus", "KP1", "KP2", "KP3", "KP0", "KPDot", "103rd",
"F13", "102nd", "F11", "F12", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "KPEnter", "RightCtrl", "KPSlash", "SysRq",
"RightAlt", "LineFeed", "Home", "Up", "PageUp", "Left", "Right", "End", "Down", "PageDown", "Insert", "Delete", "Macro", "Mute",
"VolumeDown", "VolumeUp", "Power", "KPEqual", "KPPlusMinus", "Pause", "F21", "F22", "F23", "F24", "JPN", "LeftMeta", "RightMeta",
"Compose", "Stop", "Again", "Props", "Undo", "Front", "Copy", "Open", "Paste", "Find", "Cut", "Help", "Menu", "Calc", "Setup",
"Sleep", "WakeUp", "File", "SendFile", "DeleteFile", "X-fer", "Prog1", "Prog2", "WWW", "MSDOS", "Coffee", "Direction",
"CycleWindows", "Mail", "Bookmarks", "Computer", "Back", "Forward", "CloseCD", "EjectCD", "EjectCloseCD", "NextSong", "PlayPause",
"PreviousSong", "StopCD", "Record", "Rewind", "Phone", "ISOKey", "Config", "HomePage", "Refresh", "Exit", "Move", "Edit", "ScrollUp",
"ScrollDown", "KPLeftParenthesis", "KPRightParenthesis",
NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
"Btn0", "Btn1", "Btn2", "Btn3", "Btn4", "Btn5", "Btn6", "Btn7", "Btn8", "Btn9",
NULL, NULL,  NULL, NULL, NULL, NULL,
"LeftBtn", "RightBtn", "MiddleBtn", "SideBtn", "ExtraBtn", "ForwardBtn", "BackBtn",
NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
"Trigger", "ThumbBtn", "ThumbBtn2", "TopBtn", "TopBtn2", "PinkieBtn",
"BaseBtn", "BaseBtn2", "BaseBtn3", "BaseBtn4", "BaseBtn5", "BaseBtn6",
NULL, NULL, NULL, NULL,
"BtnA", "BtnB", "BtnC", "BtnX", "BtnY", "BtnZ", "BtnTL", "BtnTR", "BtnTL2", "BtnTR2", "BtnSelect", "BtnStart", "BtnMode",
NULL, NULL, NULL,
"ToolPen", "ToolRubber", "ToolBrush", "ToolPencil", "ToolAirbrush", "ToolFinger", "ToolMouse", "ToolLens", NULL, NULL,
"Touch", "Stylus", "Stylus2" };


/*
char **names[EV_MAX + 1] = { events, keys, relatives, absolutes, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
NULL, NULL, leds, sounds, repeats };
*/

int
tunLookUpKey (char *string)
{
  int i, max = sizeof (tun_names_keys) / sizeof (char*);

  for (i = 0; i < max; i++)
    if (tun_names_keys [i] && StrCaseCmp (tun_names_keys [i], string) == 0)
      return i;

  return -1;
}

int
tunLookUpAbsValuator (char *string)
{
  int  i, max = sizeof (tun_names_abs) /sizeof (char *);

  for (i = 0; i < max; i++)
    if (tun_names_abs [i] && StrCaseCmp (tun_names_abs [i], string) == 0)
      return i;

  return -1;
}

int
tunLookUpRelValuator (char *string)
{
  int  i, max = sizeof (tun_names_rel) /sizeof (char *);

  for (i = 0; i < max; i++)
    if (tun_names_rel [i] && StrCaseCmp (tun_names_rel [i], string) == 0)
      return i;

  return -1;
}

/**/

char*
tunGetKeyName (int id)
{
  if ((id < 0) || (id >= KEY_MAX))
    return "<- Key id out of bounds !!! ->";

  return tun_names_keys [id] ? tun_names_keys [id] : "???";
}

char*
tunGetRelValuatorName (int id)
{
  if ((id < 0) || (id >= REL_MAX))
    return "<- Relative valuator id out of bounds !!! ->";

  return tun_names_abs [id] ? tun_names_abs [id] : "???";
}

char*
tunGetAbsValuatorName (int id)
{
  if ((id < 0) || (id >= ABS_MAX))
    return "<- Absolutive valuator id out of bounds !!! ->";

  return tun_names_abs [id] ? tun_names_abs [id] : "???";
}

char*
tunGetEventName (int id)
{
  if ((id < 0) || (id >= EV_MAX))
    return "<- Absolutive valuator id out of bounds !!! ->";

  return tun_names_events [id] ? tun_names_events [id] : "???";
}


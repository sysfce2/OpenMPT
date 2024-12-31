/*
 * WindowMessages.h
 * ----------------
 * Purpose: IDs of custom window messages and their parameters.
 * Notes  : (currently none)
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once

#include "openmpt/all/BuildSettings.hpp"

OPENMPT_NAMESPACE_BEGIN

// Custom window messages
enum
{
	WM_MOD_UPDATEPOSITION = (WM_USER + 1973),
	WM_MOD_INVALIDATEPATTERNS,
	WM_MOD_ACTIVATEVIEW,
	WM_MOD_CHANGEVIEWCLASS,
	WM_MOD_UNLOCKCONTROLS,
	WM_MOD_CTRLMSG,
	WM_MOD_VIEWMSG,
	WM_MOD_MIDIMSG,
	WM_MOD_DRAGONDROPPING,
	WM_MOD_KBDNOTIFY,
	WM_MOD_INSTRSELECTED,
	WM_MOD_KEYCOMMAND,
	WM_MOD_RECORDPARAM,
	WM_MOD_PLUGPARAMAUTOMATE,
	WM_MOD_MIDIMAPPING,
	WM_MOD_UPDATEVIEWS,
	WM_MOD_SETMODIFIED,
	WM_MOD_MDIACTIVATE,
	WM_MOD_MDIDEACTIVATE,
	WM_MOD_UPDATENOTIFY,
	WM_MOD_PLUGINDRYWETRATIOCHANGED,
};

enum
{
	MPT_WM_APP_UPDATECHECK_START = WM_APP + 1,
	MPT_WM_APP_UPDATECHECK_PROGRESS = WM_APP + 2,
	MPT_WM_APP_UPDATECHECK_CANCELED = WM_APP + 3,
	MPT_WM_APP_UPDATECHECK_FAILURE = WM_APP + 4,
	MPT_WM_APP_UPDATECHECK_SUCCESS = WM_APP + 5,
};

enum
{
	CTRLMSG_BASE = 0,
	CTRLMSG_SETVIEWWND,
	CTRLMSG_ACTIVATEPAGE,
	CTRLMSG_DEACTIVATEPAGE,
	CTRLMSG_SETFOCUS,
	// Pattern-Specific
	CTRLMSG_GETCURRENTPATTERN,
	CTRLMSG_NOTIFYCURRENTORDER,
	CTRLMSG_SETCURRENTORDER,
	CTRLMSG_GETCURRENTORDER,
	CTRLMSG_FORCEREFRESH,
	CTRLMSG_PAT_PREVINSTRUMENT,
	CTRLMSG_PAT_NEXTINSTRUMENT,
	CTRLMSG_PAT_SETINSTRUMENT,
	CTRLMSG_PAT_FOLLOWSONG,
	CTRLMSG_PAT_LOOP,
	CTRLMSG_PAT_NEWPATTERN,
	CTRLMSG_PAT_SETSEQUENCE,
	CTRLMSG_PAT_SETORDERLISTFOCUS,
	CTRLMSG_GETCURRENTINSTRUMENT,
	CTRLMSG_SETCURRENTINSTRUMENT,
	CTRLMSG_SETSPACING,
	CTRLMSG_PATTERNCHANGED,
	CTRLMSG_PREVORDER,
	CTRLMSG_NEXTORDER,
	CTRLMSG_SETRECORD,
	CTRLMSG_TOGGLE_OVERFLOW_PASTE,
	CTRLMSG_PAT_DUPPATTERN,
	CTRLMSG_PAT_UPDATE_TOOLBAR,
	// Sample-Specific
	CTRLMSG_SMP_PREVINSTRUMENT,
	CTRLMSG_SMP_NEXTINSTRUMENT,
	CTRLMSG_SMP_OPENFILE,
	CTRLMSG_SMP_SETZOOM,
	CTRLMSG_SMP_GETZOOM,
	CTRLMSG_SMP_SONGDROP,
	CTRLMSG_SMP_INITOPL,
	CTRLMSG_SMP_NEWSAMPLE,
	// Instrument-Specific
	CTRLMSG_INS_PREVINSTRUMENT,
	CTRLMSG_INS_NEXTINSTRUMENT,
	CTRLMSG_INS_OPENFILE,
	CTRLMSG_INS_NEWINSTRUMENT,
	CTRLMSG_INS_SONGDROP,
	CTRLMSG_INS_SAMPLEMAP,
};

enum
{
	VIEWMSG_BASE = 0,
	VIEWMSG_SETCTRLWND,
	VIEWMSG_SETACTIVE,
	VIEWMSG_SETFOCUS,
	VIEWMSG_SAVESTATE,
	VIEWMSG_LOADSTATE,
	// Pattern-Specific
	VIEWMSG_SETCURRENTPATTERN,
	VIEWMSG_GETCURRENTPATTERN,
	VIEWMSG_FOLLOWSONG,
	VIEWMSG_PATTERNLOOP,
	VIEWMSG_GETCURRENTPOS,
	VIEWMSG_SETRECORD,
	VIEWMSG_SETSPACING,
	VIEWMSG_PATTERNPROPERTIES,
	VIEWMSG_SETVUMETERS,
	VIEWMSG_SETPLUGINNAMES,
	VIEWMSG_EXPANDPATTERN,
	VIEWMSG_SHRINKPATTERN,
	VIEWMSG_COPYPATTERN,
	VIEWMSG_PASTEPATTERN,
	VIEWMSG_AMPLIFYPATTERN,
	VIEWMSG_GETDETAIL,
	VIEWMSG_SETDETAIL,
	// Sample-Specific
	VIEWMSG_SETCURRENTSAMPLE,
	VIEWMSG_SETMODIFIED,
	VIEWMSG_PREPAREUNDO,
	// Instrument-Specific
	VIEWMSG_SETCURRENTINSTRUMENT,
	VIEWMSG_DOSCROLL,
};

OPENMPT_NAMESPACE_END

/*	SCCS Id: @(#)ntconf.h	3.4	2002/03/10	*/
/* Copyright (c) NetHack PC Development Team 1993, 1994.  */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef NTCONF_H
#define NTCONF_H

#define SHELL 		/* nt use of pcsys routines caused a hang */

#define RANDOM		/* have Berkeley random(3) */

#define EXEPATH			/* Allow .exe location to be used as HACKDIR */
#define TRADITIONAL_GLYPHMAP	/* Store glyph mappings at level change time */
#if defined(WIN32CON) && !defined(__CYGWIN__)
# define LAN_FEATURES		/* Include code for lan-aware features. */
#endif

#define PC_LOCKING		/* Prevent overwrites of aborted or in-progress games */
				/* without first receiving confirmation. */

#define HOLD_LOCKFILE_OPEN	/* Keep an exclusive lock on the .0 file */

#define SELF_RECOVER		/* Allow the game itself to recover from an aborted game */

//#define USER_SOUNDS
/*
 * -----------------------------------------------------------------
 *  The remaining code shouldn't need modification.
 * -----------------------------------------------------------------
 */
#ifdef MICRO
#undef MICRO			/* never define this! */
#endif

#define NOCWD_ASSUMPTIONS	/* Always define this. There are assumptions that
                                   it is defined for WIN32.
				   Allow paths to be specified for HACKDIR,
				   LEVELDIR, SAVEDIR, BONESDIR, DATADIR,
				   SCOREDIR, LOCKDIR, CONFIGDIR, and TROUBLEDIR */
#define NO_TERMS

#ifdef NH_OPTIONS_USED
#undef NH_OPTIONS_USED
#endif
#define NH_OPTIONS_USED	"ttyoptions"

#define NH_OPTIONS_FILE NH_OPTIONS_USED

#define PORT_HELP	"porthelp"

#ifdef WIN32CON
#define PORT_DEBUG	/* include ability to debug international keyboard issues */
#endif

/* Stuff to help the user with some common, yet significant errors */
#define INTERJECT_PANIC		0
#define INTERJECTION_TYPES	(INTERJECT_PANIC + 1)
extern void interject_assistance(int,int,void *,void *);
extern void interject(int);

/* The following is needed for prototypes of certain functions */
#if defined(_MSC_VER)
#include <process.h>	/* Provides prototypes of exit(), spawn()      */
#endif

#include <string.h>	/* Provides prototypes of strncmpi(), etc.     */
#ifdef STRNCMPI
#ifndef __CYGWIN__
#define strncmpi(a,b,c) strnicmp(a,b,c)
#endif
#endif

#include <sys/types.h>
#include <stdlib.h>

#define PATHLEN		BUFSZ /* maximum pathlength */
#define FILENAME	BUFSZ /* maximum filename length (conservative) */

#if defined(_MAX_PATH) && defined(_MAX_FNAME)
# if (_MAX_PATH < BUFSZ) && (_MAX_FNAME < BUFSZ)
#undef PATHLEN
#undef FILENAME
#define PATHLEN		_MAX_PATH
#define FILENAME	_MAX_FNAME
# endif
#endif


#define NO_SIGNAL
#define index	strchr
#define rindex	strrchr
#include <time.h>
#define USE_STDARG
#ifdef RANDOM
/* Use the high quality random number routines. */
#define Rand()	random()
#else
#define Rand()	rand()
#endif

#define FCMASK	0660	/* file creation mask */
#define regularize	nt_regularize
#define HLOCK "NHPERM"

#ifndef M
#define M(c)		((char) (0x80 | (c)))
/* #define M(c)		((c) - 128) */
#endif

#ifndef C
#define C(c)		(0x1f & (c))
#endif

#if defined(DLB)
#define FILENAME_CMP  stricmp		      /* case insensitive */
#endif

#if 0
extern char levels[], bones[], permbones[],
#endif /* 0 */

/* this was part of the MICRO stuff in the past */
extern const char *alllevels, *allbones;
extern char hackdir[];
#define ABORT C('a')
#define getuid() 1
#define getlogin() (NULL)
extern void win32_abort(void);
#ifdef WIN32CON
extern void nttty_preference_update(const char *);
extern void toggle_mouse_support(void);
extern void map_subkeyvalue(char *);
extern void load_keyboard_handler(void);
#endif

#include <fcntl.h>

#ifdef LAN_FEATURES
#define MAX_LAN_USERNAME 20
#define LAN_RO_PLAYGROUND	/* not implemented in 3.3.0 */
#define LAN_SHARED_BONES	/* not implemented in 3.3.0 */
#include "nhlan.h"
#endif

#ifndef alloca
#define ALLOCA_HACK	/* used in util/panic.c */
#endif

#ifdef _MSC_VER
#if 0
#pragma warning(disable:4018)	/* signed/unsigned mismatch */
#pragma warning(disable:4305)	/* init, conv from 'const int' to 'char' */
#endif
#pragma warning(disable:4761)	/* integral size mismatch in arg; conv supp*/
#ifdef YYPREFIX
#pragma warning(disable:4102)	/* unreferenced label */
#endif
#endif

extern int set_win32_option(const char *, const char *);
#ifdef WIN32CON
#define LEFTBUTTON  FROM_LEFT_1ST_BUTTON_PRESSED
#define RIGHTBUTTON RIGHTMOST_BUTTON_PRESSED
#define MIDBUTTON   FROM_LEFT_2ND_BUTTON_PRESSED
#define MOUSEMASK (LEFTBUTTON | RIGHTBUTTON | MIDBUTTON)
#endif /* WIN32CON */

#endif /* NTCONF_H */

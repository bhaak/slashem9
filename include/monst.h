/*	SCCS Id: @(#)monst.h	3.4	1999/01/04	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef MONST_H
#define MONST_H

/* The weapon_check flag is used two ways:
 * 1) When calling mon_wield_item, is 2-6 depending on what is desired.
 * 2) Between calls to mon_wield_item, is 0 or 1 depending on whether or not
 *    the weapon is known by the monster to be cursed (so it shouldn't bother
 *    trying for another weapon).
 * I originally planned to also use 0 if the monster already had its best
 * weapon, to avoid the overhead of a call to mon_wield_item, but it turns out
 * that there are enough situations which might make a monster change its
 * weapon that this is impractical.  --KAA
 */
#define NO_WEAPON_WANTED   0
#define NEED_WEAPON	   1
#define NEED_RANGED_WEAPON 2
#define NEED_HTH_WEAPON	   3
#define NEED_PICK_AXE	   4
#define NEED_AXE	   5
#define NEED_PICK_OR_AXE   6

/* The following flags are used for the second argument to display_minventory
 * in invent.c:
 *
 * MINV_NOLET  If set, don't display inventory letters on monster's inventory.
 * MINV_ALL    If set, display all items in monster's inventory, otherwise
 *	       just display wielded weapons and worn items.
 */
#define MINV_NOLET 0x01
#define MINV_ALL   0x02

#ifndef MEXTRA_H
#include "mextra.h"
#endif

struct monst {
	struct monst *nmon;
	struct permonst *data;
	unsigned m_id;
	short mnum;	/* permanent monster index number */
	short cham;	/* if shapeshifter, orig mons[] idx goes here */
#define CHAM_ORDINARY 0	/* note: lycanthropes are handled elsewhere */
	short movement;	/* movement points (derived from permonst definition and added effects */
	uchar m_lev;	/* adjusted difficulty level of monster */
	aligntyp malign;/* alignment of this monster, relative to the
			   player (positive = good to kill) */
	xchar mx, my;
	xchar mux, muy;	/* where the monster thinks you are */
#define MTSZ 4
	coord mtrack[MTSZ];/* monster track */
	int mhp, mhpmax;
	int m_en, m_enmax;	/* Power level (for spells, etc.) */
	unsigned mappearance;	/* for undetected mimics and the wiz */
	uchar m_ap_type;	/* what mappearance is describing: */
#define M_AP_NOTHING   0	/* mappearance is unused -- monster appears
				   as itself */
#define M_AP_FURNITURE 1	/* stairs, a door, an altar, etc. */
#define M_AP_OBJECT    2	/* an object */
#define M_AP_MONSTER   3	/* a monster */

	schar mtame;			/* level of tameness, implies peaceful */
	unsigned long mintrinsics;	/* initialized from mresists */
	int mspec_used;			/* monster's special ability attack timeout */
	int oldmonnm;			/* Old monster number - for polymorph */

	bool female;		/* is female */
	bool minvis;		/* currently invisible */
	bool invis_blkd;	/* invisibility blocked */
	bool perminvis;		/* intrinsic minvis value */
	bool mundetected;	/* not seen in present hiding place */
	/* implies one of M1_CONCEAL or M1_HIDE,
	 * but not mimic (that is, snake, spider,
	 * trapper, piercer, eel)
	 */

	bool mcan;		/* has been cancelled */
	bool mburied;		/* has been buried */
	Bitfield(mspeed, 2);	/* current speed */
	Bitfield(permspeed, 2); /* intrinsic mspeed value */
	bool mrevived;		/* has been revived from the dead */
	bool mavenge;		/* did something to deserve retaliation */

	bool mflee;	       /* fleeing */
	Bitfield(mfleetim, 7); /* timeout for mflee */

	bool mcansee;	       /* cansee 1, temp.blinded 0, blind 0 */
	Bitfield(mblinded, 7); /* cansee 0, temp.blinded n, blind 0 */

	bool mcanmove; /* paralysis, similar to mblinded */
	Bitfield(mfrozen, 7);

	bool msleeping; /* asleep until woken */
	bool mstun;	/* stunned (off balance) */
	bool mconf;	/* confused */
	bool mpeaceful; /* does not attack unprovoked */
	bool mtrapped;	/* trapped in a pit, web or bear trap */
	bool mleashed;	/* monster is on a leash */
	bool isspell;	/* is a temporary spell being */
	bool uexp;	/* you get experience for its kills */

	bool mtraitor;	     /* Former pet that turned traitor */
	bool isshk;	     /* is shopkeeper */
	bool isminion;	     /* is a minion */
	bool isgd;	     /* is guard */
	bool isgyp;	     /* is a gypsy */
	bool ispriest;	     /* is a priest */
	bool iswiz;	     /* is the Wizard of Yendor */
	Bitfield(wormno, 5); /* at most 31 worms on any level */
#define MAX_NUM_WORMS 32     /* should be 2^(wormno bitfield size) */

	long mstrategy;		    /* for monsters with mflag3: current strategy */
#define STRAT_ARRIVE	0x40000000L /* just arrived on current level */
#define STRAT_WAITFORU	0x20000000L
#define STRAT_CLOSE	0x10000000L
#define STRAT_WAITMASK	0x30000000L
#define STRAT_HEAL	0x08000000L
#define STRAT_GROUND	0x04000000L
#define STRAT_MONSTR	0x02000000L
#define STRAT_PLAYER	0x01000000L
#define STRAT_NONE	0x00000000L
#define STRAT_STRATMASK 0x0f000000L
#define STRAT_XMASK	0x00ff0000L
#define STRAT_YMASK	0x0000ff00L
#define STRAT_GOAL	0x000000ffL
#define STRAT_GOALX(s)	((xchar)((s & STRAT_XMASK) >> 16))
#define STRAT_GOALY(s)	((xchar)((s & STRAT_YMASK) >> 8))

	long mtrapseen; /* bitmap of traps we've been trapped in */
	long mlstmv;	/* for catching up with lost time */
	struct obj *minvent;

	struct obj *mw;
	long misc_worn_check;
	xchar weapon_check;

	int meating;		// monster is eating timeout
	struct mextra *mextra;	// pointer to mextra struct
};

#define newmonst(xl) new(struct monst)

/* these are in mspeed */
#define MSLOW 1 /* slow monster */
#define MFAST 2 /* speeded monster */

#define MON_WEP(mon)   ((mon)->mw)
#define MON_NOWEP(mon) ((mon)->mw = NULL)

#define DEADMONSTER(mon)	((mon)->mhp < 1)


#endif /* MONST_H */

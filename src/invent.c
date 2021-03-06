/*	SCCS Id: @(#)invent.c	3.4	2003/12/02	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

#define NOINVSYM      '#'
#define CONTAINED_SYM '>' /* designator for inside a container */

static void reorder_invent(void);
static boolean mergable(struct obj *, struct obj *);
static void invdisp_nothing(const char *, const char *);
static bool worn_wield_only(struct obj *);
static bool only_here(struct obj *);
static void compactify(char *);
static boolean taking_off(const char *);
static boolean putting_on(const char *);
static int ckunpaid(struct obj *);
static int ckvalidcat(struct obj *);
static char display_pickinv(const char *, boolean, long *);
static char display_used_invlets(char);
static bool this_type_only(struct obj *);
static void dounpaid(void);
static struct obj *find_unpaid(struct obj *, struct obj **);
static void menu_identify(int);
static boolean tool_in_use(struct obj *);
static char obj_to_let(struct obj *);

/* define for getobj() */
#define FOLLOW(curr, flags) \
	(((flags)&BY_NEXTHERE) ? (curr)->nexthere : (curr)->nobj)

static int lastinvnr = 51; /* 0 ... 51 (never saved&restored) */

/* wizards can wish for venom, which will become an invisible inventory
 * item without this.  putting it in inv_order would mean venom would
 * suddenly become a choice for all the inventory-class commands, which
 * would probably cause mass confusion.  the test for inventory venom
 * is only WIZARD and not wizard because the wizard can leave venom lying
 * around on a bones level for normal players to find.
 */
static char venom_inv[] = {VENOM_CLASS, 0}; /* (constant) */

void assigninvlet(struct obj *otmp) {
	boolean inuse[52];
	int i;
	struct obj *obj;

	/* There is only one of these in inventory... */
	if (otmp->oclass == COIN_CLASS) {
		otmp->invlet = GOLD_SYM;
		return;
	}

	for (i = 0; i < 52; i++)
		inuse[i] = false;
	for (obj = invent; obj; obj = obj->nobj)
		if (obj != otmp) {
			i = obj->invlet;
			if ('a' <= i && i <= 'z')
				inuse[i - 'a'] = true;
			else if ('A' <= i && i <= 'Z')
				inuse[i - 'A' + 26] = true;
			if (i == otmp->invlet) otmp->invlet = 0;
		}
	if ((i = otmp->invlet) &&
	    (('a' <= i && i <= 'z') || ('A' <= i && i <= 'Z')))
		return;
	for (i = lastinvnr + 1; i != lastinvnr; i++) {
		if (i == 52) {
			i = -1;
			continue;
		}
		if (!inuse[i]) break;
	}
	otmp->invlet = (inuse[i] ? NOINVSYM :
				   (i < 26) ? ('a' + i) : ('A' + i - 26));
	lastinvnr = i;
}

/* note: assumes ASCII; toggling a bit puts lowercase in front of uppercase */
#define inv_rank(o) ((o)->invlet ^ 040)

/* sort the inventory; used by addinv() and doorganize() */
static void reorder_invent(void) {
	struct obj *otmp, *prev, *next;
	boolean need_more_sorting;

	do {
		/*
		 * We expect at most one item to be out of order, so this
		 * isn't nearly as inefficient as it may first appear.
		 */
		need_more_sorting = false;
		for (otmp = invent, prev = 0; otmp;) {
			next = otmp->nobj;
			if (next && inv_rank(next) < inv_rank(otmp)) {
				need_more_sorting = true;
				if (prev)
					prev->nobj = next;
				else
					invent = next;
				otmp->nobj = next->nobj;
				next->nobj = otmp;
				prev = next;
			} else {
				prev = otmp;
				otmp = next;
			}
		}
	} while (need_more_sorting);
}

#undef inv_rank

/* KMH, balance patch -- Idea by Wolfgang von Hansen <wvh@geodesy.inka.de>.
 * Harmless to character, yet deliciously evil.
 * Somewhat expensive, so don't use it often.
 *
 * Some players who depend upon fixinv complained.  They take damage
 * instead.
 */
int jumble_pack(void) {
	struct obj *obj, *nobj, *otmp;
	char let;
	int dmg = 0;

	for (obj = invent; obj; obj = nobj) {
		nobj = obj->nobj;
		if (rn2(10))
			/* Skip it */;
		else if (flags.invlet_constant)
			dmg += 2;
		else {
			/* Remove it from the inventory list (but don't touch the obj) */
			extract_nobj(obj, &invent);

			/* Determine the new letter */
			let = rnd(52) + 'A';
			if (let > 'Z')
				let = let - 'Z' + 'a' - 1;

			/* Does another object share this letter? */
			for (otmp = invent; otmp; otmp = otmp->nobj)
				if (otmp->invlet == let)
					otmp->invlet = obj->invlet;

			/* Add the item back into the inventory */
			obj->invlet = let;
			obj->nobj = invent; /* insert at beginning */
			obj->where = OBJ_INVENT;
			invent = obj;
		}
	}

	/* Clean up */
	reorder_invent();
	return dmg;
}

/* scan a list of objects to see whether another object will merge with
   one of them; used in pickup.c when all 52 inventory slots are in use,
   to figure out whether another object could still be picked up */
struct obj *merge_choice(struct obj *objlist, struct obj *obj) {
	struct monst *shkp;
	int save_nocharge;

	if (obj->otyp == SCR_SCARE_MONSTER) /* punt on these */
		return NULL;
	/* if this is an item on the shop floor, the attributes it will
	   have when carried are different from what they are now; prevent
	   that from eliciting an incorrect result from mergable() */
	save_nocharge = obj->no_charge;
	if (objlist == invent && obj->where == OBJ_FLOOR &&
	    (shkp = shop_keeper(inside_shop(obj->ox, obj->oy))) != 0) {
		if (obj->no_charge) obj->no_charge = 0;
		/* A billable object won't have its `unpaid' bit set, so would
		   erroneously seem to be a candidate to merge with a similar
		   ordinary object.  That's no good, because once it's really
		   picked up, it won't merge after all.  It might merge with
		   another unpaid object, but we can't check that here (depends
		   too much upon shk's bill) and if it doesn't merge it would
		   end up in the '#' overflow inventory slot, so reject it now. */
		else if (inhishop(shkp))
			return NULL;
	}
	while (objlist) {
		if (mergable(objlist, obj)) break;
		objlist = objlist->nobj;
	}
	obj->no_charge = save_nocharge;
	return objlist;
}

/* merge obj with otmp and delete obj if types agree */
boolean merged(struct obj **potmp, struct obj **pobj) {
	struct obj *otmp = *potmp, *obj = *pobj;

	if (mergable(otmp, obj)) {
		/* Approximate age: we do it this way because if we were to
		 * do it "accurately" (merge only when ages are identical)
		 * we'd wind up never merging any corpses.
		 * otmp->age = otmp->age*(1-proportion) + obj->age*proportion;
		 *
		 * Don't do the age manipulation if lit.  We would need
		 * to stop the burn on both items, then merge the age,
		 * then restart the burn.
		 */
		if (!obj->lamplit)
			otmp->age = ((otmp->age * otmp->quan) + (obj->age * obj->quan)) / (otmp->quan + obj->quan);

		otmp->quan += obj->quan;

		/* temporary special case for gold objects!!!! */
		if (otmp->oclass == COIN_CLASS)
			otmp->owt = weight(otmp);
		else
			otmp->owt += obj->owt;
		if (!otmp->onamelth && obj->onamelth)
			otmp = *potmp = oname(otmp, ONAME(obj));
		obj_extract_self(obj);

		/* really should merge the timeouts */
		if (obj->lamplit) obj_merge_light_sources(obj, otmp);
		if (obj->timed) obj_stop_timers(obj); /* follows lights */

		/* fixup for `#adjust' merging wielded darts, daggers, &c */
		if (obj->owornmask && carried(otmp)) {
			long wmask = otmp->owornmask | obj->owornmask;

			/* Both the items might be worn in competing slots;
			   merger preference (regardless of which is which):
			 primary weapon + alternate weapon -> primary weapon;
			 primary weapon + quiver -> primary weapon;
			 alternate weapon + quiver -> alternate weapon.
			   (Prior to 3.3.0, it was not possible for the two
			   stacks to be worn in different slots and `obj'
			   didn't need to be unworn when merging.) */
			if (wmask & W_WEP)
				wmask = W_WEP;
			else if (wmask & W_SWAPWEP)
				wmask = W_SWAPWEP;
			else if (wmask & W_QUIVER)
				wmask = W_QUIVER;
			else {
				impossible("merging strangely worn items (%lx)", wmask);
				wmask = otmp->owornmask;
			}
			if ((otmp->owornmask & ~wmask) != 0L) setnotworn(otmp);
			setworn(otmp, wmask);
			setnotworn(obj);
		}
#if 0
		/* (this should not be necessary, since items
		    already in a monster's inventory don't ever get
		    merged into other objects [only vice versa]) */
		else if (obj->owornmask && mcarried(otmp)) {
			if (obj == MON_WEP(otmp->ocarry)) {
				MON_WEP(otmp->ocarry) = otmp;
				otmp->owornmask = W_WEP;
			}
		}
#endif /*0*/

		obfree(obj, otmp); /* free(obj), bill->otmp */
		return true;
	}
	return false;
}

/*
Adjust hero intrinsics as if this object was being added to the hero's
inventory.  Called _before_ the object has been added to the hero's
inventory.

This is called when adding objects to the hero's inventory normally (via
addinv) or when an object in the hero's inventory has been polymorphed
in-place.

It may be valid to merge this code with with addinv_core2().
*/
void addinv_core1(struct obj *obj) {
	if (obj->oclass == COIN_CLASS) {
		context.botl = 1;
	} else if (obj->otyp == AMULET_OF_YENDOR) {
		if (u.uhave.amulet) impossible("already have amulet?");
		u.uhave.amulet = 1;
		achieve.get_amulet = 1;
	} else if (obj->otyp == CANDELABRUM_OF_INVOCATION) {
		if (u.uhave.menorah) impossible("already have candelabrum?");
		u.uhave.menorah = 1;
		achieve.get_candelabrum = 1;
	} else if (obj->otyp == BELL_OF_OPENING) {
		if (u.uhave.bell) impossible("already have silver bell?");
		u.uhave.bell = 1;
		achieve.get_bell = 1;
	} else if (obj->otyp == SPE_BOOK_OF_THE_DEAD) {
		if (u.uhave.book) impossible("already have the book?");
		u.uhave.book = 1;
		achieve.get_book = 1;
	} else if (obj->oartifact) {
		if (is_quest_artifact(obj)) {
			if (u.uhave.questart)
				impossible("already have quest artifact?");
			u.uhave.questart = 1;
			artitouch();
		}
		set_artifact_intrinsic(obj, true, W_ART);
	}

	if (obj->otyp == LUCKSTONE && obj->record_achieve_special) {
		achieve.get_luckstone = 1;
		obj->record_achieve_special = 0;
	} else if ((obj->otyp == AMULET_OF_REFLECTION ||
		    obj->otyp == BAG_OF_HOLDING) &&
		   obj->record_achieve_special) {
		achieve.finish_sokoban = 1;
		obj->record_achieve_special = 0;
	}
}

/*
Adjust hero intrinsics as if this object was being added to the hero's
inventory.  Called _after_ the object has been added to the hero's
inventory.

This is called when adding objects to the hero's inventory normally (via
addinv) or when an object in the hero's inventory has been polymorphed
in-place.
*/
void addinv_core2(struct obj *obj) {
	if (confers_luck(obj)) {
		/* new luckstone must be in inventory by this point
		 * for correct calculation */
		set_moreluck();
	}

	/* KMH, balance patch -- recalculate health if you've gained healthstones */
	if (obj->otyp == HEALTHSTONE)
		recalc_health();
}

/*
 * Add obj to the hero's inventory.  Make sure the object is "free".
 * Adjust hero attributes as necessary.
 */
struct obj *addinv(struct obj *obj) {
	struct obj *otmp, *prev;

	if (obj->where != OBJ_FREE)
		panic("addinv: obj not free");
	obj->no_charge = 0; /* not meaningful for invent */

	addinv_core1(obj);

	/* merge with quiver in preference to any other inventory slot
	 * in case quiver and wielded weapon are both eligible; adding
	 * extra to quivered stack is more useful than to wielded one
	 */
	if (uquiver && merged(&uquiver, &obj)) {
		obj = uquiver;
		goto added;
	}

	/* merge if possible; find end of chain in the process */
	for (prev = 0, otmp = invent; otmp; prev = otmp, otmp = otmp->nobj)
		if (merged(&otmp, &obj)) {
			obj = otmp;
			goto added;
		}
	/* didn't merge, so insert into chain */
	if (flags.invlet_constant || !prev) {
		if (flags.invlet_constant) assigninvlet(obj);
		obj->nobj = invent; /* insert at beginning */
		invent = obj;
		if (flags.invlet_constant) reorder_invent();
	} else {
		prev->nobj = obj; /* insert at end */
		obj->nobj = 0;
	}
	obj->where = OBJ_INVENT;

added:
	addinv_core2(obj);
	carry_obj_effects(&youmonst, obj); /* carrying affects the obj */
	update_inventory();
	return obj;
}
/*
 * Some objects are affected by being carried.
 * Make those adjustments here. Called _after_ the object
 * has been added to the hero's or monster's inventory,
 * and after hero's intrinsics have been updated.
 */
void carry_obj_effects(struct monst *mon, struct obj *obj) {
	/* Cursed figurines can spontaneously transform
	   when carried. */
	if (obj->otyp == FIGURINE) {
		if (obj->cursed && obj->corpsenm != NON_PM && !dead_species(obj->corpsenm, true)) {
			attach_fig_transform_timeout(obj);
		}
	} else if (obj->otyp == TORCH && obj->lamplit) {
		/* MRKR: extinguish torches before putting them */
		/*       away. Should monsters do the same?  */

		if (mon == &youmonst) {
			pline("You extinguish %s before putting it away.",
			      yname(obj));
			end_burn(obj, true);
		}
	}
}

/* Add an item to the inventory unless we're fumbling or it refuses to be
 * held (via touch_artifact), and give a message.
 * If there aren't any free inventory slots, we'll drop it instead.
 * If both success and failure messages are NULL, then we're just doing the
 * fumbling/slot-limit checking for a silent grab.  In any case,
 * touch_artifact will print its own messages if they are warranted.
 */
struct obj *hold_another_object(struct obj *obj, const char *drop_fmt, const char *drop_arg, const char *hold_msg) {
	char buf[BUFSZ];

	if (!Blind && (!obj->oinvis || See_invisible)) obj->dknown = 1;

	if (obj->oartifact) {
		/* place_object may change these */
		boolean crysknife = (obj->otyp == CRYSKNIFE);
		int oerode = obj->oerodeproof;
		boolean wasUpolyd = Upolyd;

		/* in case touching this object turns out to be fatal */
		place_object(obj, u.ux, u.uy);

		if (!touch_artifact(obj, &youmonst)) {
			obj_extract_self(obj); /* remove it from the floor */
			dropy(obj);	       /* now put it back again :-) */
			return obj;
		} else if (wasUpolyd && !Upolyd) {
			/* loose your grip if you revert your form */
			if (drop_fmt) pline(drop_fmt, drop_arg);
			obj_extract_self(obj);
			dropy(obj);
			return obj;
		}
		obj_extract_self(obj);
		if (crysknife) {
			obj->otyp = CRYSKNIFE;
			obj->oerodeproof = oerode;
		}
	}
	if (Fumbling) {
		if (drop_fmt) pline(drop_fmt, drop_arg);
		dropy(obj);
	} else {
		long oquan = obj->quan;
		int prev_encumbr = near_capacity(); /* before addinv() */

		/* encumbrance only matters if it would now become worse
		   than max( current_value, stressed ) */
		if (prev_encumbr < MOD_ENCUMBER) prev_encumbr = MOD_ENCUMBER;
		/* addinv() may redraw the entire inventory, overwriting
		   drop_arg when it comes from something like doname() */
		if (drop_arg) drop_arg = strcpy(buf, drop_arg);

		obj = addinv(obj);
		if (inv_cnt() > 52 || ((obj->otyp != LOADSTONE || !obj->cursed) && near_capacity() > prev_encumbr)) {
			if (drop_fmt) pline(drop_fmt, drop_arg);
			/* undo any merge which took place */
			if (obj->quan > oquan) obj = splitobj(obj, oquan);
			dropx(obj);
		} else {
			if (hold_msg || drop_fmt) prinv(hold_msg, obj, oquan);
		}
	}
	return obj;
}

/* useup() all of an item regardless of its quantity */
void useupall(struct obj *obj) {
	if (Has_contents(obj)) delete_contents(obj);
	setnotworn(obj);
	freeinv(obj);
	obfree(obj, NULL);
}

void useup(struct obj *obj) {
	/*  Note:  This works correctly for containers because they */
	/*	   (containers) don't merge.			    */
	if (obj->quan > 1L) {
		obj->in_use = false; /* no longer in use */
		obj->quan--;
		obj->owt = weight(obj);
		update_inventory();
	} else {
		useupall(obj);
	}
}

/* use one charge from an item and possibly incur shop debt for it */
void consume_obj_charge(struct obj *obj, boolean maybe_unpaid /* false if caller handles shop billing */) {
	if (maybe_unpaid) check_unpaid(obj);
	obj->spe -= 1;
	if (obj->known) update_inventory();
}

/*
Adjust hero's attributes as if this object was being removed from the
hero's inventory.  This should only be called from freeinv() and
where we are polymorphing an object already in the hero's inventory.

Should think of a better name...
*/
void freeinv_core(struct obj *obj) {
	if (obj->oclass == COIN_CLASS) {
		context.botl = 1;
		return;
	} else if (obj->otyp == AMULET_OF_YENDOR) {
		if (!u.uhave.amulet) impossible("don't have amulet?");
		u.uhave.amulet = 0;
	} else if (obj->otyp == CANDELABRUM_OF_INVOCATION) {
		if (!u.uhave.menorah) impossible("don't have candelabrum?");
		u.uhave.menorah = 0;
	} else if (obj->otyp == BELL_OF_OPENING) {
		if (!u.uhave.bell) impossible("don't have silver bell?");
		u.uhave.bell = 0;
	} else if (obj->otyp == SPE_BOOK_OF_THE_DEAD) {
		if (!u.uhave.book) impossible("don't have the book?");
		u.uhave.book = 0;
	} else if (obj->oartifact) {
		if (is_quest_artifact(obj)) {
			if (!u.uhave.questart)
				impossible("don't have quest artifact?");
			u.uhave.questart = 0;
		}
		set_artifact_intrinsic(obj, false, W_ART);
	}

	if (obj->otyp == LOADSTONE) {
		curse(obj);
	} else if (confers_luck(obj)) {
		set_moreluck();
		context.botl = 1;
	} else if (obj->otyp == HEALTHSTONE) {
		/* KMH, balance patch -- recalculate health if you've lost healthstones */
		recalc_health();
	} else if (obj->otyp == FIGURINE && obj->timed) {
		stop_timer(FIG_TRANSFORM, obj_to_any(obj));
	}
}

/* remove an object from the hero's inventory */
void freeinv(struct obj *obj) {
	extract_nobj(obj, &invent);
	freeinv_core(obj);
	update_inventory();
}

void delallobj(int x, int y) {
	struct obj *otmp, *otmp2;

	for (otmp = level.objects[x][y]; otmp; otmp = otmp2) {
		if (otmp == uball)
			unpunish();
		/* after unpunish(), or might get deallocated chain */
		otmp2 = otmp->nexthere;
		if (otmp == uchain)
			continue;
		delobj(otmp);
	}
}

/* destroy object in fobj chain (if unpaid, it remains on the bill) */
void delobj(struct obj *obj) {
	boolean update_map;

	if (evades_destruction(obj)) {
		/* player might be doing something stupid, but we
		 * can't guarantee that.  assume special artifacts
		 * are indestructible via drawbridges, and exploding
		 * chests, and golem creation, and ...
		 */
		return;
	}
	update_map = (obj->where == OBJ_FLOOR || (Has_contents(obj) &&
						  (obj->where == OBJ_INVENT || obj->where == OBJ_MINVENT)));
	if (Has_contents(obj)) delete_contents(obj);
	obj_extract_self(obj);
	if (update_map) newsym(obj->ox, obj->oy);
	obfree(obj, NULL);
}

// try to find a particular type of object at designated map location
struct obj *sobj_at(int otyp, int x, int y) {
	struct obj *otmp;

	for (otmp = level.objects[x][y]; otmp; otmp = otmp->nexthere)
		if (otmp->otyp == otyp)
			return otmp;

	return NULL;
}

// sobj_at(&c) traversal -- find next object of specified type
struct obj *nxtobj(struct obj *obj, int type, bool by_nexthere) {
	// start with the object after this one
	struct obj *otmp = obj;

	do {
		otmp = !by_nexthere ? otmp->nobj : otmp->nexthere;
		if (!otmp) break;
	} while (otmp->otyp != type);

	return otmp;
}


struct obj *carrying(int type) {
	struct obj *otmp;

	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (otmp->otyp == type)
			return otmp;

	return NULL;
}

const char *currency(long amount) {
	if (amount == 1L)
		return "zorkmid";

	else
		return "zorkmids";
}

boolean have_lizard(void) {
	struct obj *otmp;

	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (otmp->otyp == CORPSE && otmp->corpsenm == PM_LIZARD)
			return true;
	return false;
}

struct obj *o_on(uint id, struct obj *objchn) {
	struct obj *temp;

	while (objchn) {
		if (objchn->o_id == id) return objchn;
		if (Has_contents(objchn) && (temp = o_on(id, objchn->cobj)))
			return temp;
		objchn = objchn->nobj;
	}

	return NULL;
}

boolean obj_here(struct obj *obj, int x, int y) {
	struct obj *otmp;

	for (otmp = level.objects[x][y]; otmp; otmp = otmp->nexthere)
		if (obj == otmp)
			return true;

	return false;
}

struct obj *g_at(int x, int y) {
	struct obj *obj = level.objects[x][y];
	while (obj) {
		if (obj->oclass == COIN_CLASS)
			return obj;

		obj = obj->nexthere;
	}

	return NULL;
}

// compact a string of inventory letters by dashing runs of letters
static void compactify(char *buf) {
	int i1 = 1, i2 = 1;
	char ilet, ilet1, ilet2;

	ilet2 = buf[0];
	ilet1 = buf[1];
	buf[++i2] = buf[++i1];
	ilet = buf[i1];
	while (ilet) {
		if (ilet == ilet1 + 1) {
			if (ilet1 == ilet2 + 1)
				buf[i2 - 1] = ilet1 = '-';
			else if (ilet2 == '-') {
				buf[i2 - 1] = ++ilet1;
				buf[i2] = buf[++i1];
				ilet = buf[i1];
				continue;
			}
		}
		ilet2 = ilet1;
		ilet1 = ilet;
		buf[++i2] = buf[++i1];
		ilet = buf[i1];
	}
}

/* match the prompt for either 'T' or 'R' command */
static boolean taking_off(const char *action) {
	return !strcmp(action, "take off") || !strcmp(action, "remove");
}

/* match the prompt for either 'W' or 'P' command */
static boolean putting_on(const char *action) {
	return !strcmp(action, "wear") || !strcmp(action, "put on");
}

static int ugly_checks(const char *let, const char *word, struct obj *otmp) {
	int otyp = otmp->otyp;
	/* ugly check: remove inappropriate things */
	if ((taking_off(word) &&
	     (!(otmp->owornmask & (W_ARMOR | W_RING | W_AMUL | W_TOOL)) || (otmp == uarm && uarmc) || (otmp == uarmu && (uarm || uarmc)))) ||
	    (putting_on(word) &&
	     (otmp->owornmask & (W_ARMOR | W_RING | W_AMUL | W_TOOL)))
	/* already worn */
	    || (!strcmp(word, "ready") &&
		(otmp == uwep || (otmp == uswapwep && u.twoweap)))) {
		return 1;
	}

	/* Second ugly check; unlike the first it won't trigger an
	 * "else" in "you don't have anything else to ___".
	 */
	else if ((putting_on(word) &&
		  ((otmp->oclass == FOOD_CLASS && otmp->otyp != MEAT_RING) ||
		   (otmp->oclass == TOOL_CLASS &&
		    otyp != BLINDFOLD && otyp != TOWEL && otyp != LENSES)))
		 /*add check for improving*/
		 || ((!strcmp(word, "wield") || !strcmp(word, "improve")) &&
		     (otmp->oclass == TOOL_CLASS && !is_weptool(otmp))) ||
		 (!strcmp(word, "eat") && !is_edible(otmp)) || (!strcmp(word, "revive") && otyp != CORPSE) /* revive */
		 || (!strcmp(word, "sacrifice") &&
		     (otyp != CORPSE &&
		      otyp != SEVERED_HAND &&
		      otyp != EYEBALL && /* KMH -- fixed */
		      otyp != AMULET_OF_YENDOR && otyp != FAKE_AMULET_OF_YENDOR)) ||
		 (!strcmp(word, "write with") &&
		  (otmp->oclass == TOOL_CLASS &&
		   (!is_lightsaber(otmp) || !otmp->lamplit) &&
		   otyp != MAGIC_MARKER && otyp != TOWEL)) ||
		 (!strcmp(word, "tin") &&
		  (otyp != CORPSE || !tinnable(otmp))) ||
		 (!strcmp(word, "rub") &&
		  ((otmp->oclass == TOOL_CLASS &&
		    otyp != OIL_LAMP && otyp != MAGIC_LAMP &&
		    otyp != BRASS_LANTERN) ||
		   (otmp->oclass == GEM_CLASS && !is_graystone(otmp)))) ||
		 (!strncmp(word, "rub on the stone", 16) &&
		  *let == GEM_CLASS && /* using known touchstone */
		  otmp->dknown && objects[otyp].oc_name_known) ||
		 ((!strcmp(word, "use or apply") ||
		   !strcmp(word, "untrap with")) &&
		  /* Picks, axes, pole-weapons, bullwhips */
		  ((otmp->oclass == WEAPON_CLASS && !is_pick(otmp) &&
		    otyp != SUBMACHINE_GUN &&
		    otyp != AUTO_SHOTGUN &&
		    otyp != ASSAULT_RIFLE &&
		    otyp != FRAG_GRENADE &&
		    otyp != GAS_GRENADE &&
		    otyp != STICK_OF_DYNAMITE &&
		    !is_axe(otmp) && !is_pole(otmp) && otyp != BULLWHIP) ||
		   (otmp->oclass == POTION_CLASS &&
		    /* only applicable potion is oil, and it will only
	                      be offered as a choice when already discovered */
		    (otyp != POT_OIL || !otmp->dknown ||
		     !objects[POT_OIL].oc_name_known) &&
		    /* water is only for untrapping */
		    (strcmp(word, "untrap with") ||
		     otyp != POT_WATER || !otmp->dknown ||
		     !objects[POT_WATER].oc_name_known)) ||
		   (otmp->oclass == FOOD_CLASS &&
		    otyp != CREAM_PIE && otyp != EUCALYPTUS_LEAF) ||
		   (otmp->oclass == GEM_CLASS && !is_graystone(otmp)))) ||
		 (!strcmp(word, "invoke") &&
		  (!otmp->oartifact && !objects[otyp].oc_unique &&
		   (otyp != FAKE_AMULET_OF_YENDOR || otmp->known) &&
		   otyp != CRYSTAL_BALL && /* #invoke synonym for apply */
		   /* note: presenting the possibility of invoking non-artifact
	                        mirrors and/or lamps is a simply a cruel deception... */
		   otyp != MIRROR && otyp != MAGIC_LAMP &&
		   (otyp != OIL_LAMP || /* don't list known oil lamp */
		    (otmp->dknown && objects[OIL_LAMP].oc_name_known)))) ||
		 (!strcmp(word, "untrap with") &&
		  (otmp->oclass == TOOL_CLASS && otyp != CAN_OF_GREASE)) ||
		 (!strcmp(word, "charge") && !is_chargeable(otmp)) || (!strcmp(word, "poison") && !is_poisonable(otmp)) || ((!strcmp(word, "draw blood with") || !strcmp(word, "bandage your wounds with")) && (otmp->oclass == TOOL_CLASS && otyp != MEDICAL_KIT)))
		return 2;
	else
		return 0;
}

/* List of valid classes for allow_ugly callback */
static char valid_ugly_classes[MAXOCLASSES + 1];

/* Action word for allow_ugly callback */
static const char *ugly_word;

static bool allow_ugly(struct obj *obj) {
	return index(valid_ugly_classes, obj->oclass) &&
	       !ugly_checks(valid_ugly_classes, ugly_word, obj);
}

/*
 * getobj returns:
 *	struct obj *xxx:	object to do something with.
 *	NULL	error return: no object.
 *	&zeroobj		explicitly no object (as in w-).
 *	&thisplace		this place (as in r.).
!!!! test if gold can be used in unusual ways (eaten etc.)
!!!! may be able to remove "usegold"
 */
struct obj *getobj(const char *let, const char *word) {
	struct obj *otmp;
	char ilet;
	char buf[BUFSZ], qbuf[QBUFSZ];
	char lets[BUFSZ], altlets[BUFSZ], *ap;
	int foo = 0;
	char *bp = buf;
	xchar allowcnt = 0;	 /* 0, 1 or 2 */
	boolean usegold = false; /* can't use gold because its illegal */
	boolean allowall = false;
	boolean allownone = false;
	boolean allowfloor = false;
	boolean usefloor = false;
	boolean allowthisplace = false;
	boolean useboulder = false;
	xchar foox = 0;
	long cnt;
	boolean prezero = false;
	long dummymask;
	int ugly;
	struct obj *floorchain = NULL;
	int floorfollow = 0;

	if (*let == ALLOW_COUNT) let++, allowcnt = 1;
	if (*let == COIN_CLASS) let++, usegold = true;

	/* Equivalent of an "ugly check" for gold */
	if (usegold && !strcmp(word, "eat") &&
	    (!metallivorous(youmonst.data) || youmonst.data == &mons[PM_RUST_MONSTER]))
		usegold = false;

	if (*let == ALL_CLASSES) let++, allowall = true;
	if (*let == ALLOW_NONE) let++, allownone = true;
	if (*let == ALLOW_FLOOROBJ) {
		let++;
		if (!u.uswallow) {
			floorchain = can_reach_floorobj() ? level.objects[u.ux][u.uy] :
							    NULL;
			floorfollow = BY_NEXTHERE;
		} else {
			floorchain = u.ustuck->minvent;
			floorfollow = 0; /* nobj */
		}
		usefloor = true;
		allowfloor = !!floorchain;
	}
	if (*let == ALLOW_THISPLACE) let++, allowthisplace = true;
	/* "ugly check" for reading fortune cookies, part 1 */
	/* The normal 'ugly check' keeps the object on the inventory list.
	 * We don't want to do that for shirts/cookies, so the check for
	 * them is handled a bit differently (and also requires that we set
	 * allowall in the caller)
	 */
	if (allowall && !strcmp(word, "read")) allowall = false;

	/* another ugly check: show boulders (not statues) */
	if (*let == WEAPON_CLASS &&
	    !strcmp(word, "throw") && throws_rocks(youmonst.data))
		useboulder = true;

	if (allownone) *bp++ = '-';
	if (bp > buf && bp[-1] == '-') *bp++ = ' ';
	ap = altlets;

	ilet = 'a';
	for (otmp = invent; otmp; otmp = otmp->nobj) {
		if (!flags.invlet_constant)
			if (otmp->invlet != GOLD_SYM) /* don't reassign this */
				otmp->invlet = ilet;  /* reassign() */
		if (!*let || index(let, otmp->oclass) || (usegold && otmp->invlet == GOLD_SYM) || (useboulder && otmp->otyp == BOULDER)) {
			bp[foo++] = otmp->invlet;

			/* ugly checks */
			ugly = ugly_checks(let, word, otmp);
			if (ugly == 1) {
				foo--;
				foox++;
			} else if (ugly == 2)
				foo--;
			/* ugly check for unworn armor that can't be worn */
			else if (putting_on(word) && *let == ARMOR_CLASS &&
				 !canwearobj(otmp, &dummymask, false)) {
				foo--;
				allowall = true;
				*ap++ = otmp->invlet;
			}
		} else {
			/* "ugly check" for reading fortune cookies, part 2 */
			if ((!strcmp(word, "read") &&
			     (otmp->otyp == FORTUNE_COOKIE || otmp->otyp == T_SHIRT)))
				allowall = true;
		}

		if (ilet == 'z')
			ilet = 'A';
		else
			ilet++;
	}
	bp[foo] = 0;
	if (foo == 0 && bp > buf && bp[-1] == ' ') *--bp = 0;
	strcpy(lets, bp); /* necessary since we destroy buf */
	if (foo > 5)	  /* compactify string */
		compactify(bp);
	*ap = '\0';

	if (allowfloor && !allowall) {
		if (usegold) {
			valid_ugly_classes[0] = COIN_CLASS;
			strcpy(valid_ugly_classes + 1, let);
		} else
			strcpy(valid_ugly_classes, let);
		ugly_word = word;
		for (otmp = floorchain; otmp; otmp = FOLLOW(otmp, floorfollow))
			if (allow_ugly(otmp))
				break;
		if (!otmp)
			allowfloor = false;
	}

	if (!foo && !allowall && !allownone && !allowfloor && !allowthisplace) {
		pline("You don't have anything %sto %s.",
		      foox ? "else " : "", word);
		return NULL;
	}

	for (;;) {
		cnt = 0;
		if (allowcnt == 2) allowcnt = 1; /* abort previous count */
		sprintf(qbuf, "What do you want to %s? [", word);
		bp = eos(qbuf);
		if (buf[0]) {
			sprintf(bp, "%s or ?", buf);
			bp = eos(bp);
		}
		*bp++ = '*';
		if (allowfloor)
			*bp++ = ',';
		if (allowthisplace)
			*bp++ = '.';
		if (!buf[0] && bp[-2] != '[') {
			/* "*," -> "* or ,"; "*." -> "* or ."; "*,." -> "*, or ." */
			--bp;
			sprintf(bp, " or %c", *bp);
			bp += 5;
		}
		*bp++ = ']';
		*bp = '\0';
		if (in_doagain)
			ilet = readchar();
		else
			ilet = yn_function(qbuf, NULL, '\0');
		if (ilet == '0') prezero = true;
		while (digit(ilet) && allowcnt) {
			if (ilet != '?' && ilet != '*') savech(ilet);
			cnt = 10 * cnt + (ilet - '0');
			allowcnt = 2; /* signal presence of cnt */
			ilet = readchar();
		}
		if (digit(ilet)) {
			pline("No count allowed with this command.");
			continue;
		}
		if (index(quitchars, ilet)) {
			if (flags.verbose)
				pline("%s", "Never mind.");
			return NULL;
		}
		if (ilet == '-') {
			return allownone ? &zeroobj : NULL;
		}
		if (ilet == def_oc_syms[COIN_CLASS]) {
			if (!usegold) {
				pline("You cannot %s gold.", word);
				return NULL;
			}
			if (cnt == 0 && prezero) return NULL;
			/* Historic note: early Nethack had a bug which was
			 * first reported for Larn, where trying to drop 2^32-n
			 * gold pieces was allowed, and did interesting things
			 * to your money supply.  The LRS is the tax bureau
			 * from Larn.
			 */
			if (cnt < 0) {
				pline("The LRS would be very interested to know you have that much.");
				return NULL;
			}
		}
		if (ilet == '.') {
			if (allowthisplace)
				return &thisplace;
			else {
				pline("That is a silly thing to %s.", word);
				return NULL;
			}
		}
		if (ilet == ',') {
			int n;
			menu_item *pick_list;

			if (!usefloor) {
				pline("That is a silly thing to %s.", word);
				return NULL;
			} else if (!allowfloor) {
				if (Levitation || Flying) {
					pline("You cannot reach the floor to %s while %sing.", word, Levitation ? "float" : "fly");
				} else if (uteetering_at_seen_pit()) {
					pline("You cannot reach the bottom of the pit.");
				} else {
					pline("There's nothing here to %s.", word);
				}
				return NULL;
			}
			sprintf(qbuf, "%s what?", word);
			n = query_objlist(qbuf, floorchain,
					  floorfollow | INVORDER_SORT | SIGNAL_CANCEL, &pick_list,
					  PICK_ONE, allowall ? allow_all : allow_ugly);
			if (n < 0) {
				if (flags.verbose)
					pline("%s", "Never mind.");
				return NULL;
			} else if (!n)
				continue;
			otmp = pick_list->item.a_obj;
			if (allowcnt && pick_list->count < otmp->quan)
				otmp = splitobj(otmp, pick_list->count);
			free(pick_list);
			return otmp;
		}
		if (ilet == '?' || ilet == '*') {
			char *allowed_choices = (ilet == '?') ? lets : NULL;
			long ctmp = 0;

			if (ilet == '?' && !*lets && *altlets)
				allowed_choices = altlets;
			ilet = display_pickinv(allowed_choices, true,
					       allowcnt ? &ctmp : NULL);
			if (!ilet) continue;
			if (allowcnt && ctmp >= 0) {
				cnt = ctmp;
				if (!cnt) prezero = true;
				allowcnt = 2;
			}
			if (ilet == '\033') {
				if (flags.verbose)
					pline("%s", "Never mind.");
				return NULL;
			}
			/* they typed a letter (not a space) at the prompt */
		}
		/* WAC - throw now takes a count to allow for single/controlled shooting */
		if (allowcnt == 2 && !strcmp(word, "throw")) {
			/* permit counts for throwing gold, but don't accept
			 * counts for other things since the throw code will
			 * split off a single item anyway */
			if (ilet != def_oc_syms[COIN_CLASS])
				allowcnt = 1;
			if (cnt == 0 && prezero) return NULL;
			if (cnt == 1) {
				save_cm = (char *)1; /* Non zero */
				multi = 0;
			}
			if (cnt > 1) {
				/* pline("You can only throw one item at a time.");
				continue; */
				multi = cnt - 1;
				cnt = 1;
			}
		}
		context.botl = 1; /* May have changed the amount of money */
		savech(ilet);
		for (otmp = invent; otmp; otmp = otmp->nobj)
			if (otmp->invlet == ilet) break;
		if (!otmp) {
			pline("You don't have that object.");
			if (in_doagain) return NULL;
			continue;
		} else if (cnt < 0 || otmp->quan < cnt) {
			pline("You don't have that many!  You have only %ld.",
			      otmp->quan);
			if (in_doagain) return NULL;
			continue;
		}
		break;
	}

	if (!allowall && let && !index(let, otmp->oclass) && !(usegold && otmp->oclass == COIN_CLASS)) {
		silly_thing(word, otmp);
		return NULL;
	}
	if (allowcnt == 2) { /* cnt given */
		if (cnt == 0) return NULL;
		if (cnt != otmp->quan) {
			/* don't split a stack of cursed loadstones */
			if (otmp->otyp == LOADSTONE && otmp->cursed)
				/* kludge for canletgo()'s can't-drop-this message */
				otmp->corpsenm = (int)cnt;
			else
				otmp = splitobj(otmp, cnt);
		}
	}
	return otmp;
}

void silly_thing(const char *word, struct obj *otmp) {
	const char *s1, *s2, *s3, *what;
	int ocls = otmp->oclass, otyp = otmp->otyp;

	s1 = s2 = s3 = 0;
	/* check for attempted use of accessory commands ('P','R') on armor
	   and for corresponding armor commands ('W','T') on accessories */
	if (ocls == ARMOR_CLASS) {
		if (!strcmp(word, "put on"))
			s1 = "W", s2 = "wear", s3 = "";
		else if (!strcmp(word, "remove"))
			s1 = "T", s2 = "take", s3 = " off";
	} else if ((ocls == RING_CLASS || otyp == MEAT_RING) ||
		   ocls == AMULET_CLASS ||
		   (otyp == BLINDFOLD || otyp == TOWEL || otyp == LENSES)) {
		if (!strcmp(word, "wear"))
			s1 = "P", s2 = "put", s3 = " on";
		else if (!strcmp(word, "take off"))
			s1 = "R", s2 = "remove", s3 = "";
	}
	if (s1) {
		what = "that";
		/* quantity for armor and accessory objects is always 1,
		   but some things should be referred to as plural */
		if (otyp == LENSES || is_gloves(otmp) || is_boots(otmp))
			what = "those";
		pline("Use the '%s' command to %s %s%s.", s1, s2, what, s3);
	} else {
		pline("That is a silly thing to %s.", word);
	}
}

static int ckvalidcat(struct obj *otmp) {
	/* use allow_category() from pickup.c */
	return allow_category(otmp);
}

static int ckunpaid(struct obj *otmp) {
	return otmp->unpaid;
}

boolean wearing_armor(void) {
	return uarm || uarmc || uarmf || uarmg || uarmh || uarms || uarmu;
}

bool is_worn(struct obj *otmp) {
	return otmp->owornmask & (W_ARMOR | W_RING | W_AMUL | W_TOOL | W_SADDLE | W_WEP | W_SWAPWEP | W_QUIVER);
}

static const char removeables[] =
	{ARMOR_CLASS, WEAPON_CLASS, RING_CLASS, AMULET_CLASS, TOOL_CLASS, 0};

/* interactive version of getobj - used for Drop, Identify and */
/* Takeoff (A). Return the number of times fn was called successfully */
/* If combo is true, we just use this to get a category list */
int ggetobj(const char *word, int (*fn)(struct obj *), int mx, boolean combo /* combination menu flag */, unsigned *resultflags) {
	int (*ckfn)(struct obj *) = NULL;
	bool (*filter)(struct obj *) = NULL;
	boolean takeoff, ident, allflag, m_seen;
	int itemcount;
	int oletct, iletct, unpaid, oc_of_sym;
	char sym, *ip, olets[MAXOCLASSES + 5], ilets[MAXOCLASSES + 5];
	char extra_removeables[3 + 1]; /* uwep,uswapwep,uquiver */
	char buf[BUFSZ], qbuf[QBUFSZ];

	if (resultflags) *resultflags = 0;
	takeoff = ident = allflag = m_seen = false;
	if (!invent) {
		pline("You have nothing to %s.", word);
		return 0;
	}
	add_valid_menu_class(0); /* reset */
	if (taking_off(word)) {
		takeoff = true;
		filter = is_worn;
	} else if (!strcmp(word, "identify")) {
		ident = true;
		filter = not_fully_identified;
	}

	iletct = collect_obj_classes(ilets, invent, false, filter, &itemcount);
	unpaid = count_unpaid(invent);

	if (ident && !iletct) {
		return -1; /* no further identifications */
	} else if (!takeoff && (unpaid || invent)) {
		ilets[iletct++] = ' ';
		if (unpaid) ilets[iletct++] = 'u';
		if (count_buc(invent, BUC_BLESSED)) ilets[iletct++] = 'B';
		if (count_buc(invent, BUC_UNCURSED)) ilets[iletct++] = 'U';
		if (count_buc(invent, BUC_CURSED)) ilets[iletct++] = 'C';
		if (count_buc(invent, BUC_UNKNOWN)) ilets[iletct++] = 'X';
		if (invent) ilets[iletct++] = 'a';
	} else if (takeoff && invent) {
		ilets[iletct++] = ' ';
	}
	ilets[iletct++] = 'i';
	if (!combo)
		ilets[iletct++] = 'm'; /* allow menu presentation on request */
	ilets[iletct] = '\0';

	for (;;) {
		sprintf(qbuf, "What kinds of thing do you want to %s? [%s]",
			word, ilets);
		getlin(qbuf, buf);
		if (buf[0] == '\033') return 0;
		if (index(buf, 'i')) {
			if (display_inventory(NULL, true) == '\033') return 0;
		} else
			break;
	}

	extra_removeables[0] = '\0';
	if (takeoff) {
		/* arbitrary types of items can be placed in the weapon slots
		   [any duplicate entries in extra_removeables[] won't matter] */
		if (uwep) strkitten(extra_removeables, uwep->oclass);
		if (uswapwep) strkitten(extra_removeables, uswapwep->oclass);
		if (uquiver) strkitten(extra_removeables, uquiver->oclass);
	}

	ip = buf;
	olets[oletct = 0] = '\0';
	while ((sym = *ip++) != '\0') {
		if (sym == ' ') continue;
		oc_of_sym = def_char_to_objclass(sym);
		if (takeoff && oc_of_sym != MAXOCLASSES) {
			if (index(extra_removeables, oc_of_sym)) {
				; /* skip rest of takeoff checks */
			} else if (!index(removeables, oc_of_sym)) {
				pline("Not applicable.");
				return 0;
			} else if (oc_of_sym == ARMOR_CLASS && !wearing_armor()) {
				pline("You are not wearing any armor.");
				return 0;
			} else if (oc_of_sym == WEAPON_CLASS &&
				   !uwep && !uswapwep && !uquiver) {
				pline("You are not wielding anything.");
				return 0;
			} else if (oc_of_sym == RING_CLASS && !uright && !uleft) {
				pline("You are not wearing rings.");
				return 0;
			} else if (oc_of_sym == AMULET_CLASS && !uamul) {
				pline("You are not wearing an amulet.");
				return 0;
			} else if (oc_of_sym == TOOL_CLASS && !ublindf) {
				pline("You are not wearing a blindfold.");
				return 0;
			}
		}

		if (oc_of_sym == COIN_CLASS && !combo) {
			context.botl = 1;
		} else if (sym == 'a') {
			allflag = true;
		} else if (sym == 'A') {
			/* same as the default */;
		} else if (sym == 'u') {
			add_valid_menu_class('u');
			ckfn = ckunpaid;
		} else if (sym == 'B') {
			add_valid_menu_class('B');
			ckfn = ckvalidcat;
		} else if (sym == 'U') {
			add_valid_menu_class('U');
			ckfn = ckvalidcat;
		} else if (sym == 'C') {
			add_valid_menu_class('C');
			ckfn = ckvalidcat;
		} else if (sym == 'X') {
			add_valid_menu_class('X');
			ckfn = ckvalidcat;
		} else if (sym == 'm') {
			m_seen = true;
		} else if (oc_of_sym == MAXOCLASSES) {
			pline("You don't have any %c's.", sym);
		} else if (oc_of_sym != VENOM_CLASS) { /* suppress venom */
			if (!index(olets, oc_of_sym)) {
				add_valid_menu_class(oc_of_sym);
				olets[oletct++] = oc_of_sym;
				olets[oletct] = 0;
			}
		}
	}

	// wtf is the !!!!-prefixed stuff in that comment?
	// it was originally guarded by GOLDOBJ
	/// -MC
	if (m_seen)
		return (allflag || (!oletct && ckfn != ckunpaid)) ? -2 : -3;
	else if (flags.menu_style != MENU_TRADITIONAL && combo && !allflag)
		return 0;
	else /*!!!! if (allowgold == 2 && !oletct)
	    !!!! return 1;	 you dropped gold (or at least tried to)
            !!!! test gold dropping
	else*/
	{
		int cnt = askchain(&invent, olets, allflag, fn, ckfn, mx, word);
		/*
		 * askchain() has already finished the job in this case
		 * so set a special flag to convey that back to the caller
		 * so that it won't continue processing.
		 * Fix for bug C331-1 reported by Irina Rempt-Drijfhout.
		 */
		if (combo && allflag && resultflags)
			*resultflags |= ALL_FINISHED;
		return cnt;
	}
}

/*
 * Walk through the chain starting at objchn and ask for all objects
 * with olet in olets (if nonNULL) and satisfying ckfn (if nonnull)
 * whether the action in question (i.e., fn) has to be performed.
 * If allflag then no questions are asked. Max gives the max nr of
 * objects to be treated. Return the number of objects treated.
 */
int askchain(struct obj **objchn, const char *olets, int allflag, int (*fn)(struct obj *), int (*ckfn)(struct obj *), int mx, const char *word) {
	struct obj *otmp, *otmp2, *otmpo;
	char sym, ilet;
	int cnt = 0, dud = 0, tmp;
	boolean takeoff, nodot, ident, ininv;
	char qbuf[QBUFSZ];

	takeoff = taking_off(word);
	ident = !strcmp(word, "identify");
	nodot = (!strcmp(word, "nodot") || !strcmp(word, "drop") ||
		 ident || takeoff);
	ininv = (*objchn == invent);
	/* Changed so the askchain is interrogated in the order specified.
	 * For example, if a person specifies =/ then first all rings will be
	 * asked about followed by all wands -dgk
	 */
nextclass:
	ilet = 'a' - 1;
	if (*objchn && (*objchn)->oclass == COIN_CLASS)
		ilet--; /* extra iteration */
	for (otmp = *objchn; otmp; otmp = otmp2) {
		if (ilet == 'z')
			ilet = 'A';
		else
			ilet++;
		otmp2 = otmp->nobj;
		if (olets && *olets && otmp->oclass != *olets) continue;
		if (takeoff && !is_worn(otmp)) continue;
		if (ident && !not_fully_identified(otmp)) continue;
		if (ckfn && !(*ckfn)(otmp)) continue;
		if (!allflag) {
			strcpy(qbuf, !ininv ? doname(otmp) :
					      xprname(otmp, NULL, ilet, !nodot, 0L, 0L));
			strcat(qbuf, "?");
			sym = (takeoff || ident || otmp->quan < 2L) ?
				      nyaq(qbuf) :
				      nyNaq(qbuf);
		} else
			sym = 'y';

		otmpo = otmp;
		if (sym == '#') {
			/* Number was entered; split the object unless it corresponds
			   to 'none' or 'all'.  2 special cases: cursed loadstones and
			   welded weapons (eg, multiple daggers) will remain as merged
			   unit; done to avoid splitting an object that won't be
			   droppable (even if we're picking up rather than dropping).
			 */
			if (!yn_number)
				sym = 'n';
			else {
				sym = 'y';
				if (yn_number < otmp->quan && !welded(otmp) &&
				    (!otmp->cursed || otmp->otyp != LOADSTONE)) {
					otmp = splitobj(otmp, yn_number);
				}
			}
		}
		switch (sym) {
			case 'a':
				allflag = 1;
			//fallthru
			case 'y':
				tmp = (*fn)(otmp);
				if (tmp < 0) {
					if (container_gone(fn)) {
						/* otmp caused magic bag to explode;
						   both are now gone */
						otmp = 0; // and return
					} else if (otmp && otmp != otmpo) {
						// split occurred, merge again
						merged(&otmpo, &otmp);
					}
					goto ret;
				}
				cnt += tmp;
				if (--mx == 0) goto ret;
			//fallthru
			case 'n':
				if (nodot) dud++;
			default:
				break;
			case 'q':
				/* special case for seffects() */
				if (ident) cnt = -1;
				goto ret;
		}
	}
	if (olets && *olets && *++olets)
		goto nextclass;
	if (!takeoff && (dud || cnt))
		pline("That was all.");
	else if (!dud && !cnt)
		pline("No applicable objects.");
ret:
	return cnt;
}

/*
 *	Object identification routines:
 */

/* make an object actually be identified; no display updating */
void fully_identify_obj(struct obj *otmp) {
	makeknown(otmp->otyp);
	if (otmp->oartifact) discover_artifact(otmp->oartifact);
	otmp->known = otmp->dknown = otmp->bknown = otmp->rknown = 1;
	if (otmp->otyp == EGG && otmp->corpsenm != NON_PM)
		learn_egg_type(otmp->corpsenm);
}

/* ggetobj callback routine; identify an object and give immediate feedback */
int identify(struct obj *otmp) {
	fully_identify_obj(otmp);
	prinv(NULL, otmp, 0L);
	return 1;
}

/* menu of unidentified objects; select and identify up to id_limit of them */
static void menu_identify(int id_limit) {
	menu_item *pick_list;
	int n, i, first = 1;
	char buf[BUFSZ];
	/* assumptions:  id_limit > 0 and at least one unID'd item is present */

	while (id_limit) {
		sprintf(buf, "What would you like to identify %s?",
			first ? "first" : "next");
		n = query_objlist(buf, invent, SIGNAL_NOMENU | USE_INVLET | INVORDER_SORT,
				  &pick_list, PICK_ANY, not_fully_identified);

		if (n > 0) {
			if (n > id_limit) n = id_limit;
			for (i = 0; i < n; i++, id_limit--)
				identify(pick_list[i].item.a_obj);
			free(pick_list);
			mark_synch(); /* Before we loop to pop open another menu */
		} else {
			if (n < 0) pline("That was all.");
			id_limit = 0; /* Stop now */
		}
		first = 0;
	}
}

/* dialog with user to identify a given number of items; 0 means all */
void identify_pack(int id_limit) {
	struct obj *obj, *the_obj;
	int n, unid_cnt;

	unid_cnt = 0;
	the_obj = 0; /* if unid_cnt ends up 1, this will be it */
	for (obj = invent; obj; obj = obj->nobj)
		if (not_fully_identified(obj)) ++unid_cnt, the_obj = obj;

	if (!unid_cnt) {
		pline("You have already identified all of your possessions.");
	} else if (!id_limit) {
		/* identify everything */
		if (unid_cnt == 1) {
			identify(the_obj);
		} else {
			/* TODO:  use fully_identify_obj and cornline/menu/whatever here */
			for (obj = invent; obj; obj = obj->nobj)
				if (not_fully_identified(obj)) identify(obj);
		}
	} else {
		/* identify up to `id_limit' items */
		n = 0;
		if (flags.menu_style == MENU_TRADITIONAL)
			do {
				n = ggetobj("identify", identify, id_limit, false, NULL);
				if (n < 0) break; /* quit or no eligible items */
			} while ((id_limit -= n) > 0);
		if (n == 0 || n < -1)
			menu_identify(id_limit);
	}
	update_inventory();
}

// should of course only be called for things in invent
static char obj_to_let(struct obj *obj) {
	if (!flags.invlet_constant) {
		obj->invlet = NOINVSYM;
		reassign();
	}
	return obj->invlet;
}

/*
 * Print the indicated quantity of the given object.  If quan == 0L then use
 * the current quantity.
 */
void prinv(const char *prefix, struct obj *obj, long quan) {
	if (!prefix) prefix = "";
	pline("%s%s%s",
	      prefix, *prefix ? " " : "",
	      xprname(obj, NULL, obj_to_let(obj), true, 0L, quan));
}

char *xprname(struct obj *obj,
	      const char *txt, /* text to print instead of obj */
	      char let,	       /* inventory letter */
	      boolean dot,     /* append period, (dot && cost => Iu) */
	      long cost,       /* cost (for inventory of unpaid or expended items) */
	      long quan /* if non-0, print this quantity, not obj->quan */) {
	static char li[BUFSZ];
	boolean use_invlet = flags.invlet_constant && let != CONTAINED_SYM;
	long savequan = 0;

	if (quan && obj) {
		savequan = obj->quan;
		obj->quan = quan;
	}

	/*
	 * If let is:
	 *	*  Then obj == null and we are printing a total amount.
	 *	>  Then the object is contained and doesn't have an inventory letter.
	 */
	if (cost != 0 || let == '*') {
		/* if dot is true, we're doing Iu, otherwise Ix */
		sprintf(li, "%c - %-45s %6ld %s",
			(dot && use_invlet ? obj->invlet : let),
			(txt ? txt : doname(obj)), cost, currency(cost));
	} else {
		/* ordinary inventory display or pickup message */
		sprintf(li, "%c - %s%s",
			(use_invlet ? obj->invlet : let),
			(txt ? txt : doname(obj)), (dot ? "." : ""));
	}
	if (savequan) obj->quan = savequan;

	return li;
}

// the 'i' command
int ddoinv(void) {
	display_inventory(NULL, false);
	return 0;
}

/*
 * find_unpaid()
 *
 * Scan the given list of objects.  If last_found is NULL, return the first
 * unpaid object found.  If last_found is not NULL, then skip over unpaid
 * objects until last_found is reached, then set last_found to NULL so the
 * next unpaid object is returned.  This routine recursively follows
 * containers.
 */
static struct obj *find_unpaid(struct obj *list, struct obj **last_found) {
	struct obj *obj;

	while (list) {
		if (list->unpaid) {
			if (*last_found) {
				/* still looking for previous unpaid object */
				if (list == *last_found)
					*last_found = NULL;
			} else
				return *last_found = list;
		}
		if (Has_contents(list)) {
			if ((obj = find_unpaid(list->cobj, last_found)) != 0)
				return obj;
		}
		list = list->nobj;
	}
	return NULL;
}

/*
 * Internal function used by display_inventory and getobj that can display
 * inventory and return a count as well as a letter. If out_cnt is not null,
 * any count returned from the menu selection is placed here.
 */
static char display_pickinv(const char *lets, boolean want_reply, long *out_cnt) {
	struct obj *otmp;
	char ilet, ret;
	char *invlet = flags.inv_order;
	int n, classcount;
	winid win;			  /* windows being used */
	static winid local_win = WIN_ERR; /* window for partial menus */
	anything any;
	menu_item *selected;
#ifdef PROXY_GRAPHICS
	static int busy = 0;
	if (busy)
		return 0;
	busy++;
#endif

	/* overriden by global flag */
	if (flags.perm_invent) {
		win = (lets && *lets) ? local_win : WIN_INVEN;
		/* create the first time used */
		if (win == WIN_ERR)
			win = local_win = create_nhwindow(NHW_MENU);
	} else {
		win = WIN_INVEN;
	}

	/*
	 * Exit early if no inventory -- but keep going if we are doing
	 * a permanent inventory update.  We need to keep going so the
	 * permanent inventory window updates itself to remove the last
	 * item(s) dropped.  One down side:  the addition of the exception
	 * for permanent inventory window updates _can_ pop the window
	 * up when it's not displayed -- even if it's empty -- because we
	 * don't know at this level if its up or not.  This may not be
	 * an issue if empty checks are done before hand and the call
	 * to here is short circuited away.
	 */
	if (!invent && !(flags.perm_invent && !lets && !want_reply)) {
		pline("Not carrying anything.");
#ifdef PROXY_GRAPHICS
		busy--;
#endif
		return 0;
	}

	/* oxymoron? temporarily assign permanent inventory letters */
	if (!flags.invlet_constant) reassign();

	if (lets && strlen(lets) == 1) {
		/* when only one item of interest, use pline instead of menus;
		   we actually use a fake message-line menu in order to allow
		   the user to perform selection at the --More-- prompt for tty */
		ret = '\0';
		for (otmp = invent; otmp; otmp = otmp->nobj) {
			if (otmp->invlet == lets[0]) {
				ret = message_menu(lets[0],
						   want_reply ? PICK_ONE : PICK_NONE,
						   xprname(otmp, NULL, lets[0], true, 0L, 0L));
				if (out_cnt) *out_cnt = -1L; /* select all */
				break;
			}
		}
#ifdef PROXY_GRAPHICS
		busy--;
#endif
		return ret;
	}

	start_menu(win);
nextclass:
	classcount = 0;
	any.a_void = 0; /* set all bits to zero */
	for (otmp = invent; otmp; otmp = otmp->nobj) {
		ilet = otmp->invlet;
		if (!lets || !*lets || index(lets, ilet)) {
			if (!flags.sortpack || otmp->oclass == *invlet) {
				if (flags.sortpack && !classcount) {
					any.a_void = 0; /* zero */
					add_menu(win, NO_GLYPH, &any, 0, 0, iflags.menu_headings,
						 let_to_name(*invlet, false), MENU_UNSELECTED);
					classcount++;
				}
				any.a_char = ilet;
				add_menu(win, obj_to_glyph(otmp),
					 &any, ilet, 0, ATR_NONE, doname(otmp),
					 MENU_UNSELECTED);
			}
		}
	}
	if (flags.sortpack) {
		if (*++invlet) goto nextclass;
		if (--invlet != venom_inv) {
			invlet = venom_inv;
			goto nextclass;
		}
	}

	if (flags.perm_invent && !lets && !invent)
		add_menu(win, NO_GLYPH, &any, 0, 0, 0, "Not carrying anything", MENU_UNSELECTED);
	end_menu(win, NULL);

	n = select_menu(win, want_reply ? PICK_ONE : PICK_NONE, &selected);
	if (n > 0) {
		ret = selected[0].item.a_char;
		if (out_cnt) *out_cnt = selected[0].count;
		free(selected);
	} else
		ret = !n ? '\0' : '\033'; /* cancelled */

#ifdef PROXY_GRAPHICS
	busy--;
#endif
	return ret;
}

/*
 * If lets == NULL or "", list all objects in the inventory.  Otherwise,
 * list all objects with object classes that match the order in lets.
 *
 * Returns the letter identifier of a selected item, or 0 if nothing
 * was selected.
 */
char display_inventory(const char *lets, boolean want_reply) {
	return display_pickinv(lets, want_reply, NULL);
}

/*
 * Show what is current using inventory letters.
 *
 */
static char display_used_invlets(char avoidlet) {
	struct obj *otmp;
	char ilet, ret = 0;
	char *invlet = flags.inv_order;
	int n, classcount, done = 0;
	winid win;
	anything any;
	menu_item *selected;

	if (invent) {
		win = create_nhwindow(NHW_MENU);
		start_menu(win);
		while (!done) {
			any.a_void = 0;		// set all bits to zero
			classcount = 0;
			for(otmp = invent; otmp; otmp = otmp->nobj) {
				ilet = otmp->invlet;
				if (ilet == avoidlet) continue;
				if (!flags.sortpack || otmp->oclass == *invlet) {
					if (flags.sortpack && !classcount) {
						any.a_void = 0;	// zero
						add_menu(win, NO_GLYPH, &any, 0, 0, iflags.menu_headings,
								let_to_name(*invlet, false), MENU_UNSELECTED);
						classcount++;
					}
					any.a_char = ilet;
					add_menu(win, obj_to_glyph(otmp),
							&any, ilet, 0, ATR_NONE, doname(otmp),
							MENU_UNSELECTED);
				}
			}
			if (flags.sortpack && *++invlet) continue;
			done = 1;
		}
		end_menu(win, "Inventory letters used:");

		n = select_menu(win, PICK_NONE, &selected);
		if (n > 0) {
			ret = selected[0].item.a_char;
			free(selected);
		} else {
			ret = !n ? '\0' : '\033';       /* cancelled */
		}
	}
	return ret;
}

/*
 * Returns the number of unpaid items within the given list.  This includes
 * contained objects.
 */
int count_unpaid(struct obj *list) {
	int count = 0;

	while (list) {
		if (list->unpaid) count++;
		if (Has_contents(list))
			count += count_unpaid(list->cobj);
		list = list->nobj;
	}
	return count;
}

/*
 * Returns the number of items with b/u/c/unknown within the given list.
 * This does NOT include contained objects.
 */
int count_buc(struct obj *list, int type) {
	int count = 0;

	while (list) {
		if (Role_if(PM_PRIEST)) list->bknown = true;
		switch (type) {
			case BUC_BLESSED:
				if (list->oclass != COIN_CLASS && list->bknown && list->blessed)
					count++;
				break;
			case BUC_CURSED:
				if (list->oclass != COIN_CLASS && list->bknown && list->cursed)
					count++;
				break;
			case BUC_UNCURSED:
				if (list->oclass != COIN_CLASS &&
				    list->bknown && !list->blessed && !list->cursed)
					count++;
				break;
			case BUC_UNKNOWN:
				if (list->oclass != COIN_CLASS && !list->bknown)
					count++;
				break;
			default:
				impossible("need count of curse status %d?", type);
				return 0;
		}
		list = list->nobj;
	}
	return count;
}

static void dounpaid(void) {
	winid win;
	struct obj *otmp, *marker;
	char ilet;
	char *invlet = flags.inv_order;
	int classcount, count, num_so_far;
	long cost, totcost;

	count = count_unpaid(invent);

	if (count == 1) {
		marker = NULL;
		otmp = find_unpaid(invent, &marker);

		cost = unpaid_cost(otmp);
		otmp->unpaid = false; // suppress '(unpaid)' suffix
		pline("%s", xprname(otmp, distant_name(otmp, doname),
				    carried(otmp) ? otmp->invlet : CONTAINED_SYM,
				    true, cost, 0L));

		otmp->unpaid = true;
		return;
	}

	win = create_nhwindow(NHW_MENU);
	cost = totcost = 0;
	num_so_far = 0; /* count of # printed so far */
	if (!flags.invlet_constant) reassign();

	do {
		classcount = 0;
		for (otmp = invent; otmp; otmp = otmp->nobj) {
			ilet = otmp->invlet;
			if (otmp->unpaid) {
				if (!flags.sortpack || otmp->oclass == *invlet) {
					if (flags.sortpack && !classcount) {
						putstr(win, 0, let_to_name(*invlet, true));
						classcount++;
					}

					totcost += cost = unpaid_cost(otmp);
					otmp->unpaid = false; // suppress "(unpaid)" suffix
					putstr(win, 0, xprname(otmp, distant_name(otmp, doname), ilet, true, cost, 0L));
					otmp->unpaid = true;
					num_so_far++;
				}
			}
		}
	} while (flags.sortpack && (*++invlet));

	if (count > num_so_far) {
		/* something unpaid is contained */
		if (flags.sortpack)
			putstr(win, 0, let_to_name(CONTAINED_SYM, true));
		/*
		 * Search through the container objects in the inventory for
		 * unpaid items.  The top level inventory items have already
		 * been listed.
		 */
		for (otmp = invent; otmp; otmp = otmp->nobj) {
			if (Has_contents(otmp)) {
				long contcost = 0;
				marker = NULL; /* haven't found any */
				while (find_unpaid(otmp->cobj, &marker)) {
					totcost += cost = unpaid_cost(marker);
					contcost += cost;
					if (otmp->cknown) {
						marker->unpaid = false; // suppress "(unpaid)" suffix
						putstr(win, 0,
							xprname(marker, distant_name(marker, doname),
								CONTAINED_SYM, true, cost, 0L));
						marker->unpaid = true;
					}
				}
				if (!otmp->cknown) {
					char contbuf[BUFSZ];
					// Shopkeeper knows what to charge for contents
					sprintf(contbuf, "%s contents", s_suffix(xname(otmp)));
					putstr(win, 0, xprname(NULL, contbuf,
								CONTAINED_SYM, true, contcost, 0L));

				}
			}
		}
	}

	putstr(win, 0, "");
	putstr(win, 0, xprname(NULL, "Total:", '*', false, totcost, 0L));
	display_nhwindow(win, false);
	destroy_nhwindow(win);
}

/* query objlist callback: return true if obj type matches "this_type" */
static int this_type;

static bool this_type_only(struct obj *obj) {
	return obj->oclass == this_type;
}

/* the 'I' command */
int dotypeinv(void) {
	char c = '\0';
	int n, i = 0;
	char *extra_types, types[BUFSZ];
	int class_count, oclass, unpaid_count, itemcount;
	boolean billx = *u.ushops && doinvbill(0);
	menu_item *pick_list;
	boolean traditional = true;
	const char *prompt = "What type of object do you want an inventory of?";

	if (!invent && !billx) {
		pline("You aren't carrying anything.");
		return 0;
	}
	unpaid_count = count_unpaid(invent);
	if (flags.menu_style != MENU_TRADITIONAL) {
		if (flags.menu_style == MENU_FULL ||
		    flags.menu_style == MENU_PARTIAL) {
			traditional = false;
			i = UNPAID_TYPES;
			if (billx) i |= BILLED_TYPES;
			n = query_category(prompt, invent, i, &pick_list, PICK_ONE);
			if (!n) return 0;
			this_type = c = pick_list[0].item.a_int;
			free(pick_list);
		}
	}
	if (traditional) {
		/* collect a list of classes of objects carried, for use as a prompt */
		types[0] = 0;
		class_count = collect_obj_classes(types, invent, false, NULL, &itemcount);
		if (unpaid_count) {
			strcat(types, "u");
			class_count++;
		}
		if (billx) {
			strcat(types, "x");
			class_count++;
		}
		/* add everything not already included; user won't see these */
		extra_types = eos(types);
		*extra_types++ = '\033';
		if (!unpaid_count) *extra_types++ = 'u';
		if (!billx) *extra_types++ = 'x';
		*extra_types = '\0'; /* for index() */
		for (i = 0; i < MAXOCLASSES; i++)
			if (!index(types, def_oc_syms[i])) {
				*extra_types++ = def_oc_syms[i];
				*extra_types = '\0';
			}

		if (class_count > 1) {
			c = yn_function(prompt, types, '\0');
			savech(c);
			if (c == '\0') {
				clear_nhwindow(WIN_MESSAGE);
				return 0;
			}
		} else {
			/* only one thing to itemize */
			if (unpaid_count)
				c = 'u';
			else if (billx)
				c = 'x';
			else
				c = types[0];
		}
	}
	if (c == 'x') {
		if (billx)
			doinvbill(1);
		else
			pline("No used-up objects on your shopping bill.");
		return 0;
	}
	if (c == 'u') {
		if (unpaid_count)
			dounpaid();
		else
			pline("You are not carrying any unpaid objects.");
		return 0;
	}
	if (traditional) {
		oclass = def_char_to_objclass(c); /* change to object class */
		if (oclass == COIN_CLASS) {
			return doprgold();
		} else if (index(types, c) > index(types, '\033')) {
			pline("You have no such objects.");
			return 0;
		}
		this_type = oclass;
	}
	if (query_objlist(NULL, invent,
			  (flags.invlet_constant ? USE_INVLET : 0) | INVORDER_SORT,
			  &pick_list, PICK_NONE, this_type_only) > 0)
		free(pick_list);
	return 0;
}

/* return a string describing the dungeon feature at <x,y> if there
   is one worth mentioning at that location; otherwise null */
const char *dfeature_at(int x, int y, char *buf) {
	struct rm *lev = &levl[x][y];
	int ltyp = lev->typ, cmap = -1;
	const char *dfeature = 0;
	static char altbuf[BUFSZ];

	if (IS_DOOR(ltyp)) {
		switch (lev->doormask) {
			case D_NODOOR:
				cmap = S_ndoor;
				break; /* "doorway" */
			case D_ISOPEN:
				cmap = S_vodoor;
				break; /* "open door" */
			case D_BROKEN:
				dfeature = "broken door";
				break;
			default:
				cmap = S_vcdoor;
				break; /* "closed door" */
		}
		/* override door description for open drawbridge */
		if (is_drawbridge_wall(x, y) >= 0)
			dfeature = "open drawbridge portcullis", cmap = -1;
	} else if (IS_FOUNTAIN(ltyp))
		cmap = S_fountain; /* "fountain" */
	else if (IS_THRONE(ltyp))
		cmap = S_throne; /* "opulent throne" */
	else if (is_lava(x, y))
		cmap = S_lava; /* "molten lava" */
	else if (is_ice(x, y))
		cmap = S_ice; /* "ice" */
	else if (is_pool(x, y))
		dfeature = "pool of water";
	else if (IS_SINK(ltyp))
		cmap = S_sink; /* "sink" */
	else if (IS_TOILET(ltyp))
		cmap = S_toilet;
	else if (IS_ALTAR(ltyp)) {
		sprintf(altbuf, "altar to %s (%s)", a_gname(),
			align_str(Amask2align(lev->altarmask & ~AM_SHRINE)));
		dfeature = altbuf;
	} else if ((x == xupstair && y == yupstair) ||
		   (x == sstairs.sx && y == sstairs.sy && sstairs.up))
		cmap = S_upstair; /* "staircase up" */
	else if ((x == xdnstair && y == ydnstair) ||
		 (x == sstairs.sx && y == sstairs.sy && !sstairs.up))
		cmap = S_dnstair; /* "staircase down" */
	else if (x == xupladder && y == yupladder)
		cmap = S_upladder; /* "ladder up" */
	else if (x == xdnladder && y == ydnladder)
		cmap = S_dnladder; /* "ladder down" */
	else if (ltyp == DRAWBRIDGE_DOWN)
		cmap = S_vodbridge; /* "lowered drawbridge" */
	else if (ltyp == DBWALL)
		cmap = S_vcdbridge; /* "raised drawbridge" */
	else if (IS_GRAVE(ltyp))
		cmap = S_grave; /* "grave" */
	else if (ltyp == TREE)
		cmap = S_tree; /* "tree" */
	else if (ltyp == IRONBARS)
		dfeature = "set of iron bars";

	if (cmap >= 0) dfeature = sym_desc[cmap].explanation;
	if (dfeature) strcpy(buf, dfeature);
	return dfeature;
}

/* look at what is here; if there are many objects (5 or more),
   don't show them unless obj_cnt is 0 */
int look_here(int obj_cnt /* obj_cnt > 0 implies that autopickup is in progess */, boolean picked_some) {
	struct obj *otmp;
	struct trap *trap;
	const char *verb = Blind ? "feel" : "see";
	const char *dfeature = NULL;
	char fbuf[BUFSZ], fbuf2[BUFSZ];
	winid tmpwin;
	boolean skip_objects = (obj_cnt >= 5), felt_cockatrice = false;

	if (u.uswallow && u.ustuck) {
		struct monst *mtmp = u.ustuck;
		sprintf(fbuf, "Contents of %s %s",
			s_suffix(mon_nam(mtmp)), mbodypart(mtmp, STOMACH));
		/* Skip "Contents of " by using fbuf index 12 */
		pline("You %s to %s what is lying in %s.",
		      Blind ? "try" : "look around", verb, &fbuf[12]);
		otmp = mtmp->minvent;
		if (otmp) {
			for (; otmp; otmp = otmp->nobj) {
				/* If swallower is an animal, it should have become stone but... */
				if (otmp->otyp == CORPSE) feel_cockatrice(otmp, false);
			}
			if (Blind) strcpy(fbuf, "You feel");
			strcat(fbuf, ":");
			display_minventory(mtmp, MINV_ALL, fbuf);
		} else {
			pline("You %s no objects here.", verb);
		}
		return !!Blind;
	}
	if (!skip_objects && (trap = t_at(u.ux, u.uy)) && trap->tseen)
		pline("There is %s here.",
		      an(sym_desc[trap_to_defsym(trap->ttyp)].explanation));

	otmp = level.objects[u.ux][u.uy];
	dfeature = dfeature_at(u.ux, u.uy, fbuf2);
	if (dfeature && !strcmp(dfeature, "pool of water") && Underwater)
		dfeature = 0;

	if (Blind) {
		boolean drift = Is_airlevel(&u.uz) || Is_waterlevel(&u.uz);
		if (dfeature && !strncmp(dfeature, "altar ", 6)) {
			/* don't say "altar" twice, dfeature has more info */
			pline("You try to feel what is here.");
		} else {
			pline("You try to feel what is %s%s.",
			      drift ? "floating here" : "lying here on the ",
			      drift ? "" : surface(u.ux, u.uy));
		}
		if (dfeature && !drift && !strcmp(dfeature, surface(u.ux, u.uy)))
			dfeature = 0; /* ice already identifed */
		if (!can_reach_floor()) {
			pline("But you can't reach it!");
			return 0;
		}
	}

	if (dfeature) {
		sprintf(fbuf, "There is %s here.", an(dfeature));
		if (iflags.cmdassist &&
		    (IS_FOUNTAIN(levl[u.ux][u.uy].typ) ||
		     IS_SINK(levl[u.ux][u.uy].typ) ||
		     IS_TOILET(levl[u.ux][u.uy].typ)))
			strcat(fbuf, "  Use \"q.\" to drink from it.");
	}

	if (!otmp || is_lava(u.ux, u.uy) || (is_pool(u.ux, u.uy) && !Underwater)) {
		if (dfeature) pline("%s", fbuf);
		sense_engr_at(u.ux, u.uy, false); /* Eric Backus */
		if (!skip_objects && (Blind || !dfeature))
			pline("You %s no objects here.", verb);
		return !!Blind;
	}
	/* we know there is something here */

	if (skip_objects) {
		if (dfeature) pline("%s", fbuf);
		sense_engr_at(u.ux, u.uy, false); /* Eric Backus */
		pline("There are %s%s objects here.",
		      (obj_cnt <= 10) ? "several" : "many",
		      picked_some ? " more" : "");
	} else if (!otmp->nexthere) {
		/* only one object */
		if (dfeature) pline("%s", fbuf);
		sense_engr_at(u.ux, u.uy, false); /* Eric Backus */
		if (otmp->oinvis && !See_invisible) verb = "feel";
		pline("You %s here %s.", verb, doname(otmp));
		if (otmp->otyp == CORPSE) feel_cockatrice(otmp, false);
	} else {
		display_nhwindow(WIN_MESSAGE, false);
		tmpwin = create_nhwindow(NHW_MENU);
		if (dfeature) {
			putstr(tmpwin, 0, fbuf);
			putstr(tmpwin, 0, "");
		}
		putstr(tmpwin, 0, Blind ? "Things that you feel here:" : "Things that are here:");
		for (; otmp; otmp = otmp->nexthere) {
			if (otmp->otyp == CORPSE && will_feel_cockatrice(otmp, false)) {
				char buf[BUFSZ];
				felt_cockatrice = true;
				strcpy(buf, doname(otmp));
				strcat(buf, "...");
				putstr(tmpwin, 0, buf);
				break;
			}
			putstr(tmpwin, 0, doname(otmp));
		}
		display_nhwindow(tmpwin, true);
		destroy_nhwindow(tmpwin);
		if (felt_cockatrice) feel_cockatrice(otmp, false);
		sense_engr_at(u.ux, u.uy, false); /* Eric Backus */
	}
	return !!Blind;
}

/* explicilty look at what is here, including all objects */
int dolook(void) {
	return look_here(0, false);
}

boolean will_feel_cockatrice(struct obj *otmp, boolean force_touch) {
	if ((Blind || force_touch) && !uarmg && !Stone_resistance &&
	    (otmp->otyp == CORPSE && touch_petrifies(&mons[otmp->corpsenm])))
		return true;

	return false;
}

void feel_cockatrice(struct obj *otmp, boolean force_touch) {
	char kbuf[BUFSZ];

	if (will_feel_cockatrice(otmp, force_touch)) {
		if (poly_when_stoned(youmonst.data))
			pline("You touched the %s corpse with your bare %s.",
			      mons[otmp->corpsenm].mname, makeplural(body_part(HAND)));
		else
			pline("Touching the %s corpse is a fatal mistake...",
			      mons[otmp->corpsenm].mname);
		sprintf(kbuf, "%s corpse", an(mons[otmp->corpsenm].mname));
		instapetrify(kbuf);
	}
}

void stackobj(struct obj *obj) {
	struct obj *otmp;

	for (otmp = level.objects[obj->ox][obj->oy]; otmp; otmp = otmp->nexthere)
		if (otmp != obj && merged(&obj, &otmp))
			break;

	return;
}

// returns true if obj  & otmp can be merged
static boolean mergable(struct obj *otmp, struct obj *obj) {
	if (obj->otyp != otmp->otyp) return false;
	/* coins of the same kind will always merge */
	if (obj->oclass == COIN_CLASS) return true;

	if (obj->unpaid != otmp->unpaid ||
	    obj->spe != otmp->spe || obj->dknown != otmp->dknown ||
	    (obj->bknown != otmp->bknown && !Role_if(PM_PRIEST)) ||
	    obj->cursed != otmp->cursed || obj->blessed != otmp->blessed ||
	    obj->no_charge != otmp->no_charge ||
	    obj->obroken != otmp->obroken ||
	    obj->otrapped != otmp->otrapped ||
	    obj->lamplit != otmp->lamplit ||
	    (flags.pickup_thrown && obj->was_thrown != otmp->was_thrown) ||
	    obj->oinvis != otmp->oinvis ||
	    obj->oldtyp != otmp->oldtyp ||
	    obj->greased != otmp->greased ||
	    obj->oeroded != otmp->oeroded ||
	    obj->oeroded2 != otmp->oeroded2 ||
	    obj->bypass != otmp->bypass)
		return false;

	if ((obj->oclass == WEAPON_CLASS || obj->oclass == ARMOR_CLASS) &&
	    (obj->oerodeproof != otmp->oerodeproof || obj->rknown != otmp->rknown))
		return false;

	if (obj->oclass == FOOD_CLASS && (obj->oeaten != otmp->oeaten ||
					  obj->odrained != otmp->odrained || obj->orotten != otmp->orotten))
		return false;

	if (obj->otyp == CORPSE || obj->otyp == EGG || obj->otyp == TIN) {
		if (obj->corpsenm != otmp->corpsenm)
			return false;
	}

	/* armed grenades do not merge */
	if ((obj->timed || otmp->timed) && is_grenade(obj))
		return false;

	/* hatching eggs don't merge; ditto for revivable corpses */
	if ((obj->timed || otmp->timed) && (obj->otyp == EGG ||
					    (obj->otyp == CORPSE && otmp->corpsenm >= LOW_PM &&
					     is_reviver(&mons[otmp->corpsenm]))))
		return false;

	/* allow candle merging only if their ages are close */
	/* see begin_burn() for a reference for the magic "25" */
	/* [ALI] Slash'EM can't rely on using 25, because we
	 * have chosen to reduce the cost of candles such that
	 * the initial age is no longer a multiple of 25. The
	 * simplest solution is just to use 20 instead, since
	 * initial candle age is always a multiple of 20.
	 */
	if ((obj->otyp == TORCH || Is_candle(obj)) && obj->age / 20 != otmp->age / 20)
		return false;

	/* burning potions of oil never merge */
	/* MRKR: nor do burning torches */
	if ((obj->otyp == POT_OIL || obj->otyp == TORCH) && obj->lamplit)
		return false;

	/* don't merge surcharged item with base-cost item */
	if (obj->unpaid && !same_price(obj, otmp))
		return false;

	/* if they have names, make sure they're the same */
	if ((obj->onamelth != otmp->onamelth &&
	     ((obj->onamelth && otmp->onamelth) || obj->otyp == CORPSE)) ||
	    (obj->onamelth && otmp->onamelth &&
	     strncmp(ONAME(obj), ONAME(otmp), (int)obj->onamelth)))
		return false;

	/* for the moment, any additional information is incompatible */
	if (obj->oxlth || otmp->oxlth) return false;

	if (obj->oartifact != otmp->oartifact) return false;

	if (obj->known == otmp->known ||
	    !objects[otmp->otyp].oc_uses_known) {
		return objects[obj->otyp].oc_merge;
	} else
		return false;
}

int doprgold(void) {
	/* the messages used to refer to "carrying gold", but that didn't
	   take containers into account */
	long umoney = money_cnt(invent);
	if (!umoney)
		pline("Your wallet is empty.");
	else
		pline("Your wallet contains %ld %s.", umoney, currency(umoney));
	shopper_financial_report();
	return 0;
}

int doprwep(void) {
	if (!uwep) {
		if (!u.twoweap) {
			pline("You are empty %s.", body_part(HANDED));
			return 0;
		}
		/* Avoid printing "right hand empty" and "other hand empty" */
		if (!uswapwep) {
			pline("You are attacking with both %s.", makeplural(body_part(HAND)));
			return 0;
		}
		pline("Your right %s is empty.", body_part(HAND));
	} else {
		prinv(NULL, uwep, 0L);
	}
	if (u.twoweap) {
		if (uswapwep)
			prinv(NULL, uswapwep, 0L);
		else
			pline("Your other %s is empty.", body_part(HAND));
	}
	return 0;
}

int doprarm(void) {
	if (!wearing_armor())
		pline("You are not wearing any armor.");
	else {
		char lets[8];
		int ct = 0;

		if (uarmu) lets[ct++] = obj_to_let(uarmu);
		if (uarm) lets[ct++] = obj_to_let(uarm);
		if (uarmc) lets[ct++] = obj_to_let(uarmc);
		if (uarmh) lets[ct++] = obj_to_let(uarmh);
		if (uarms) lets[ct++] = obj_to_let(uarms);
		if (uarmg) lets[ct++] = obj_to_let(uarmg);
		if (uarmf) lets[ct++] = obj_to_let(uarmf);
		lets[ct] = 0;
		display_inventory(lets, false);
	}
	return 0;
}

int doprring(void) {
	if (!uleft && !uright)
		pline("You are not wearing any rings.");
	else {
		char lets[3];
		int ct = 0;

		if (uleft) lets[ct++] = obj_to_let(uleft);
		if (uright) lets[ct++] = obj_to_let(uright);
		lets[ct] = 0;
		display_inventory(lets, false);
	}
	return 0;
}

int dopramulet(void) {
	if (!uamul)
		pline("You are not wearing an amulet.");
	else
		prinv(NULL, uamul, 0L);
	return 0;
}

static boolean tool_in_use(struct obj *obj) {
	if ((obj->owornmask & (W_TOOL | W_SADDLE)) != 0L) return true;
	if (obj->oclass != TOOL_CLASS) return false;
	return obj == uwep || obj->lamplit || (obj->otyp == LEASH && obj->leashmon);
}

int doprtool(void) {
	struct obj *otmp;
	int ct = 0;
	char lets[52 + 1];

	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (tool_in_use(otmp))
			lets[ct++] = obj_to_let(otmp);
	lets[ct] = '\0';
	if (!ct)
		pline("You are not using any tools.");
	else
		display_inventory(lets, false);
	return 0;
}

/* '*' command; combines the ')' + '[' + '=' + '"' + '(' commands;
   show inventory of all currently wielded, worn, or used objects */
int doprinuse(void) {
	struct obj *otmp;
	int ct = 0;
	char lets[52 + 1];

	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (is_worn(otmp) || tool_in_use(otmp))
			lets[ct++] = obj_to_let(otmp);
	lets[ct] = '\0';
	if (!ct)
		pline("You are not wearing or wielding anything.");
	else
		display_inventory(lets, false);
	return 0;
}

/*
 * uses up an object that's on the floor, charging for it as necessary
 */
void useupf(struct obj *obj, long numused) {
	struct obj *otmp;
	boolean at_u = (obj->ox == u.ux && obj->oy == u.uy);

	/* burn_floor_paper() keeps an object pointer that it tries to
	 * useupf() multiple times, so obj must survive if plural */
	if (obj->quan > numused) {
		otmp = splitobj(obj, numused);
		obj->in_use = false; /* rest no longer in use */
	} else
		otmp = obj;
	if (costly_spot(otmp->ox, otmp->oy)) {
		if (index(u.urooms, *in_rooms(otmp->ox, otmp->oy, 0)))
			addtobill(otmp, false, false, false);
		else
			stolen_value(otmp, otmp->ox, otmp->oy, false, false, true);
	}
	delobj(otmp);
	if (at_u && u.uundetected && hides_under(youmonst.data))
		u.uundetected = OBJ_AT(u.ux, u.uy);
}

/*
 * Conversion from a class to a string for printing.
 * This must match the object class order.
 */
static const char *names[] = {0,
			      "Illegal objects", "Weapons", "Armor", "Rings", "Amulets",
			      "Tools", "Comestibles", "Potions", "Scrolls", "Spellbooks",
			      "Wands", "Coins", "Gems", "Boulders/Statues", "Iron balls",
			      "Chains", "Venoms"};

static const char oth_symbols[] = {
	CONTAINED_SYM,
	'\0'};

static const char *oth_names[] = {
	"Bagged/Boxed items"};

static char *invbuf = NULL;
static unsigned invbufsiz = 0;

char *let_to_name(char let, boolean unpaid) {
	const char *class_name;
	const char *pos;
	int oclass = (let >= 1 && let < MAXOCLASSES) ? let : 0;
	unsigned len;

	if (oclass)
		class_name = names[oclass];
	else if ((pos = index(oth_symbols, let)) != 0)
		class_name = oth_names[pos - oth_symbols];
	else
		class_name = names[0];

	len = strlen(class_name) + (unpaid ? sizeof "unpaid_" : sizeof "");
	if (len > invbufsiz) {
		if (invbuf) free(invbuf);
		invbufsiz = len + 10; /* add slop to reduce incremental realloc */
		invbuf = alloc(invbufsiz);
	}
	if (unpaid)
		strcat(strcpy(invbuf, "Unpaid "), class_name);
	else
		strcpy(invbuf, class_name);
	return invbuf;
}

// release the static buffer held by let_to_name()
void free_invbuf(void) {
	if (invbuf) {
		free(invbuf);
		invbuf = NULL;
	}

	invbufsiz = 0;
}

// give consecutive letters to every item in inventory (for !fixinv mode)
void reassign(void) {
	int i;
	struct obj *obj;

	for (obj = invent, i = 0; obj; obj = obj->nobj, i++)
		obj->invlet = (i < 26) ? ('a' + i) : ('A' + i - 26);

	lastinvnr = i;
}

/* #adjust command
 *
 *     User specifies a 'from' slot for inventory stack to move,
 *     then a 'to' slot for its destination.  Open slots and those
 *     filled by compatible stacks are listed as likely candidates
 *     but user can pick any inventory letter (including 'from').
 *     All compatible items found are gathered into the 'from'
 *     stack as it is moved.  If the 'to' slot isn't empty and
 *     doesn't merge, then its stack is swapped to the 'from' slot.
 *
 *     If the user specifies a count when choosing the 'from' slot,
 *     and that count is less than the full size of the stack,
 *     then the stack will be split.  The 'count' portion is moved
 *     to the destination, and the only candidate for merging with
 *     it is the stack already at the 'to' slot, if any.  When the
 *     destination is non-empty but won't merge, whatever is there
 *     will be moved to an open slot; if there isn't any open slot
 *     available, the adjustment attempt fails.
 *
 *     Splitting has one special case:  if 'to' slot is non-empty
 *     and is compatible with 'from' in all respects except for
 *     user-assigned names, the 'count' portion being moved is
 *     effectively renamed so that it will merge with 'to' stack.
 */
int doorganize(void) {
	struct obj *obj, *otmp, *splitting, *bumped;
	int ix, cur;
	char let;
	char alphabet[52 + 1], buf[52 + 1];
	char qbuf[QBUFSZ];
	char allowall[3]; // {ALLOW_COUNT, ALL_CLASSES, 0}
	const char *adj_type;

	if (!invent) {
		pline("You aren't carrying anything to adjust.");
		return 0;
	}

	if (!flags.invlet_constant) reassign();
	// get object the user wants to organize (the 'from' slot)
	allowall[0] = ALLOW_COUNT;
	allowall[1] = ALL_CLASSES;
	allowall[2] = '\0';
	if (!(obj = getobj(allowall, "adjust"))) return 0;

	// figure out whether user gave a split count to getobj()
	splitting = bumped = 0;
	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (otmp->nobj == obj) {	// knowledge of splitobj() operation
			if (otmp->invlet == obj->invlet) splitting = otmp;
			break;
		}



	// initialize the list with all lower and upper case letters
	for (ix = 0, let = 'a';  let <= 'z'; ) alphabet[ix++] = let++;
	for (let = 'A'; let <= 'Z'; ) alphabet[ix++] = let++;
	alphabet[ix] = '\0';

	// for floating inv letters, truncate list after the first open slot
	if (!flags.invlet_constant && (ix = inv_cnt()) < 52)
		alphabet[ix + (splitting ? 0 : 1)] = '\0';


	/* blank out all the letters currently in use in the inventory */
	/* except those that will be merged with the selected object   */
	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (otmp != obj && !mergable(otmp, obj)) {
			if (otmp->invlet <= 'Z') alphabet[(otmp->invlet) - 'A' + 26] = ' ';
			else alphabet[(otmp->invlet) - 'a'] = ' ';
		}

	/* compact the list by removing all the blanks */
	for (ix = cur = 0; ix <= alphabet[ix]; ix++)
		if (alphabet[ix] != ' ') buf[cur++] = alphabet[ix];

	buf[cur] = '\0';
	/* and by dashing runs of letters */
	if (cur > 5) compactify(buf);

	// get 'to' slot to use as inventory letter
	sprintf(qbuf, "Adjust letter to what [%s]%s?", buf, invent ? " (? to see used letters)" : "");
	for (;;) {
		let = yn_function(qbuf, NULL, '\0');
		if (let == '?' || let == '*') {
			char ilet = display_used_invlets(splitting ? obj->invlet : '\0');
			if (!ilet) continue;
			if (ilet == '\033') {
				pline("Never mind.");
				return 0;
			}
			let = ilet;
		}

		if (index(quitchars, let) ||
		    /* adjusting to same slot is meaningful since all compatible stacks
		       get collected along the way, but splitting to same slot is not */
		    (splitting && let == obj->invlet)) {

			if (splitting) merged(&splitting, &obj);

			pline("Never mind.");
			return 0;
		}

		if (letter(let) && let != '@') break; // got one
		pline("Select an inventory slot letter"); // else try again
	}

	/* change the inventory and print the resulting item */
	adj_type = splitting ? "Splitting:" : "Moving:";

	/*
	 * don't use freeinv/addinv to avoid double-touching artifacts,
	 * dousing lamps, losing luck, cursing loadstone, etc.
	 */
	extract_nobj(obj, &invent);

	for (otmp = invent; otmp; ) {
		if (!splitting) {
			if (merged(&otmp, &obj)) {
				adj_type = "Merging:";
				obj = otmp;
				otmp = otmp->nobj;
				extract_nobj(obj, &invent);
				continue;   /* otmp has already been updated */
			} else if (otmp->invlet == let) {
				adj_type = "Swapping:";
				otmp->invlet = obj->invlet;
			}
		} else {
			/* splitting: don't merge extra compatible stacks;
			   if destination is compatible, do merge with it,
			   otherwise bump whatever is there to an open slot */
			if (otmp->invlet == let) {
				int olth = obj->onamelth;

				/* ugly hack:  if these objects aren't going to merge
				   solely because they have conflicting user-assigned
				   names, strip off the name of the one being moved */
				if (olth && !obj->oartifact && !mergable(otmp, obj)) {
					obj->onamelth = 0;
					/* restore name iff merging is still not possible */
					if (!mergable(otmp, obj)) obj->onamelth = olth;
				}

				if (merged(&otmp, &obj)) {

					obj = otmp;
					extract_nobj(obj, &invent);
				} else if (inv_cnt() >= 52) {
					merged(&splitting, &obj);    /* undo split */
					/* "knapsack cannot accommodate any more items" */
					pline("Your pack is too full.");
					return 0;
				} else {
					bumped = otmp;
					extract_nobj(bumped, &invent);
				}
				break;
			} /* found 'to' slot */
		} /* splitting */
		otmp = otmp->nobj;
	}


	// inline addinv; insert loose object at beginning of inventory
	obj->invlet = let;
	obj->nobj = invent;
	obj->where = OBJ_INVENT;
	invent = obj;
	reorder_invent();

	if (bumped) {
		/* splitting the 'from' stack is causing an incompatible
		   stack in the 'to' slot to be moved into an open one;
		   we need to do another inline insertion to inventory */
		assigninvlet(bumped);
		bumped->nobj = invent;
		bumped->where = OBJ_INVENT;
		invent = bumped;
		reorder_invent();
	}

	// messages deferred until inventory has been fully reestablished
	prinv(adj_type, obj, 0L);
	if (bumped) prinv("Moving:", bumped, 0);
	update_inventory();
	return 0;
}

// common to display_minventory and display_cinventory
static void invdisp_nothing(const char *hdr, const char *txt) {
	winid win;
	anything any;
	menu_item *selected;

	any.a_void = 0;
	win = create_nhwindow(NHW_MENU);
	start_menu(win);
	add_menu(win, NO_GLYPH, &any, 0, 0, iflags.menu_headings, hdr, MENU_UNSELECTED);
	add_menu(win, NO_GLYPH, &any, 0, 0, ATR_NONE, "", MENU_UNSELECTED);
	add_menu(win, NO_GLYPH, &any, 0, 0, ATR_NONE, txt, MENU_UNSELECTED);
	end_menu(win, NULL);
	if (select_menu(win, PICK_NONE, &selected) > 0)
		free(selected);
	destroy_nhwindow(win);
	return;
}

// query_objlist callback: return things that could possibly be worn/wielded
static bool worn_wield_only(struct obj *obj) {
	return obj->oclass == WEAPON_CLASS || obj->oclass == ARMOR_CLASS || obj->oclass == AMULET_CLASS || obj->oclass == RING_CLASS || obj->oclass == TOOL_CLASS;
}

/*
 * Display a monster's inventory.
 * Returns a pointer to the object from the monster's inventory selected
 * or NULL if nothing was selected.
 *
 * By default, only worn and wielded items are displayed.  The caller
 * can pick one.  Modifier flags are:
 *
 *	MINV_NOLET	- nothing selectable
 *	MINV_ALL	- display all inventory
 */
struct obj *display_minventory(struct monst *mon, int dflags, char *title) {
	struct obj *ret;
	char tmp[QBUFSZ];
	int n;
	menu_item *selected = 0;
	int do_all = (dflags & MINV_ALL) != 0;

	sprintf(tmp, "%s %s:", s_suffix(noit_Monnam(mon)),
		do_all ? "possessions" : "armament");

	if (do_all ? (mon->minvent != 0) : (mon->misc_worn_check || MON_WEP(mon))) {
		/* Fool the 'weapon in hand' routine into
		 * displaying 'weapon in claw', etc. properly.
		 */
		struct permonst *stash_udata = youmonst.data;
		youmonst.data = mon->data;

		n = query_objlist(title ? title : tmp, mon->minvent, INVORDER_SORT, &selected,
				  (dflags & MINV_NOLET) ? PICK_NONE : PICK_ONE,
				  do_all ? allow_all : worn_wield_only);

		youmonst.data = stash_udata;
	} else {
		invdisp_nothing(title ? title : tmp, "(none)");
		n = 0;
	}

	if (n > 0) {
		ret = selected[0].item.a_obj;
		free(selected);
	} else {
		ret = NULL;
	}
	return ret;
}

/*
 * Display the contents of a container in inventory style.
 * Currently, this is only used for statues, via wand of probing.
 * [ALI] Also used when looting medical kits.
 */
struct obj *display_cinventory(struct obj *obj) {
	struct obj *ret;
	char tmp[QBUFSZ];
	int n;
	menu_item *selected = 0;

	sprintf(tmp, "Contents of %s:", doname(obj));

	if (obj->cobj) {
		n = query_objlist(tmp, obj->cobj, INVORDER_SORT, &selected,
				  PICK_NONE, allow_all);
	} else {
		invdisp_nothing(tmp, "(empty)");
		n = 0;
	}
	if (n > 0) {
		ret = selected[0].item.a_obj;
		free(selected);
	} else {
		ret = NULL;
	}
	obj->cknown = true;
	return ret;
}

/* query objlist callback: return true if obj is at given location */
static coord only;

static bool only_here(struct obj *obj) {
	return obj->ox == only.x && obj->oy == only.y;
}

/*
 * Display a list of buried items in inventory style.  Return a non-zero
 * value if there were items at that spot.
 *
 * Currently, this is only used with a wand of probing zapped downwards.
 */
int display_binventory(int x, int y, boolean as_if_seen) {
	struct obj *obj;
	menu_item *selected = 0;
	int n;

	/* count # of objects here */
	for (n = 0, obj = level.buriedobjlist; obj; obj = obj->nobj)
		if (obj->ox == x && obj->oy == y) {
			if (as_if_seen) obj->dknown = 1;
			n++;
		}

	if (n) {
		only.x = x;
		only.y = y;
		if (query_objlist("Things that are buried here:",
				  level.buriedobjlist, INVORDER_SORT,
				  &selected, PICK_NONE, only_here) > 0)
			free(selected);
		only.x = only.y = 0;
	}
	return n;
}

/*invent.c*/

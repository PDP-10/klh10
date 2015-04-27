
/*
 *  termcap.h -- definitions of the variables in which
 *		 the termcap info is kept
 */

#define TCAPDEFS \
	TCFLG(am, "has automatic margins"),\
	TCFLG(bs, "has ^H backspace"),\
	TCFLG(hc, "hardcopy term"),\
	TCFLG(km, "has meta key"),\
	TCFLG(mi, "safe to move in insert mode"),\
	TCFLG(nc, "no working CR"),\
	TCFLG(pt, "has hardware tabs"),\
	TCFLG(ul, "underscore can overstrike"),\
	TCFLG(ns, "wraps rather than scrolls"),\
	TCFLG(MT, "has meta key (xterm)"),\
	\
	TCSTR(al, "Add new blank line"),\
	TCSTR(le, "Move back one position"),\
	TCSTR(bt, "Back tab"),\
	TCSTR(cd, "Clear to end of display"),\
	TCSTR(ce, "Clear to end of line"),\
	TCSTR(ch, "Like CM but horizontal motion only"),\
	TCSTR(cl, "Clear screen"),\
	TCSTR(cm, "Cursor motion"),\
	TCSTR(cr, "Carriage return (default '^M')"),\
	TCSTR(cs, "Change scrolling region (vt100), like CM"),\
	TCSTR(cv, "Like CM but vertical only"),\
	TCSTR(dc, "Delete character"),\
	TCSTR(dl, "Delete line"),\
	TCSTR(dm, "Delete mode"),\
	TCSTR(do, "Down one line"),\
	TCSTR(ed, "End delete mode"),\
	TCSTR(ei, "End insert mode"),\
	TCSTR(ho, "Home cursor"),\
	TCSTR(ic, "Insert character"),\
	TCSTR(im, "Insert mode (enter)"),\
	TCSTR(is, "Terminal initialization string"),\
	TCSTR(ll, "Last line, first column"),\
	TCSTR(nd, "Non-destructive space (cursor right)"),\
	TCSTR(nw, "Newline (behave like CR + LF)"),\
	TCSTR(pc, "Pad character"),\
	TCSTR(sf, "Scroll forward"),\
	TCSTR(sr, "Scroll reverse"),\
	TCSTR(ta, "Tab (other than '^I' or with padding)"),\
	TCSTR(te, "String to end programs that use CM"),\
	TCSTR(ti, "String to begin programs that use CM"),\
	TCSTR(up, "Upline (cursor up)"),\
	TCSTR(vb, "Visible bell"),\
	TCSTR(ve, "Sequence to end open/visual mode"),\
	TCSTR(vs, "Sequence to start open/visual mode"),\
	TCSTR(se, "End standout mode"),\
	TCSTR(so, "Begin standout mode"),\
	TCSTR(DC, "Delete n characters"),\
	TCSTR(IC, "Insert n characters"),\
	TCSTR(DL, "Delete n lines"),\
	TCSTR(AL, "xInsert n lines),\"
	\
	TCNUM(co, "Number of columns in a line"),\
	TCNUM(dB, "Backspace delay"),\
	TCNUM(dC, "Carriage return delay"),\
	TCNUM(dN, "Newline delay"),\
	TCNUM(dT, "Tab delay "),\
	TCNUM(li, "Number of lines on screen")


enum tcaps {
#define TCSTR(tc,comm) TC_##tc##_s
#define TCNUM(tc,comm) TC_##tc##_n
#define TCFLG(tc,comm) TC_##tc##_f
		TCAPDEFS
#undef TCSTR
#undef TCNUM
#undef TCFLG
};

/* Actually declare the vars now */
#define TCSTR(tc,comm) static char *TC_##tc##_s = NULL;
#define TCNUM(tc,comm) static int   TC_##tc##_n = 0;
#define TCFLG(tc,comm) static int   TC_##tc##_f = 0;
		TCAPDEFS
#undef TCSTR
#undef TCNUM
#undef TCFLG

struct tcent {
    char *tcname;
    int   tctyp;
    void *tcval;
};
struct tcent tcaptab[] = {
#define TCSTR(tc,comm) {#tc, TCTYP_STR, (void *)&TC_##tc##_s}
#define TCNUM(tc,comm) {#tc, TCTYP_NUM, (void *)&TC_##tc##_n}}
#define TCFLG(tc,comm) {#tc, TCTYP_FLG, (void *)&TC_##tc##_f}}
		TCAPDEFS
#undef TCSTR
#undef TCNUM
#undef TCFLG
}

#define TCSTR(tc) (TC_##tc##_s)
#define TCNUM(tc) (TC_##tc##_n)
#define TCFLG(tc) (TC_##tc##_f)

#define auto_right_margin TCFLG(am)	/* Terminal has automatic margins */
#define BS		TCFLG(bs)	/* Terminal can backspace with '^H' */
#define hard_copy	TCFLG(hc)	/* Hardcopy terminal */
#define has_meta_key	TCFLG(km)	/* Has meta key */
#define move_insert_mode TCFLG(mi)	/* Safe to move while in insert mode */
#define NC		TCFLG(nc)	/* No correctly working carriage ret */
#define PT		TCFLG(pt)	/* Has hardware tabs */
#define transparent_underline TCFLG(ul)	/* '_' overstrikes */
#define no_scroll	TCFLG(ns)	/* Wraps rather than scrolls */
#define also_has_meta_key TCFLG(MT)	/* KLH: Another flag for meta key */
					/* (xterm is fond of this one) */

#define insert_line	TCSTR(al)	/* Add new blank line */
#define cursor_left	TCSTR(le)	/* Move back one position */
#define back_tab	TCSTR(bt)	/* Back tab */
#define clr_eos		TCSTR(cd)	/* Clear to end of display */
#define clr_eol		TCSTR(ce)	/* Clear to end of line */
#define CH		TCSTR(ch)	/* Like CM but horiz motion only */
#define clear_screen	TCSTR(cl)	/* Clear screen */
#define cursor_address	TCSTR(cm)	/* Cursor motion */
#define carriage_return	TCSTR(cr)	/* Carriage return (default '^M') */
#define CS		TCSTR(cs)	/* Change scrolling region (vt100), like CM */
#define CV		TCSTR(cv)	/* Like CM but vertical only */
#define delete_character TCSTR(dc)	/* Delete character */
#define delete_line	TCSTR(dl)	/* Delete line */
#define DM		TCSTR(dm)	/* Delete mode */
#define cursor_down	TCSTR(do)	/* Down one line */
#define ED		TCSTR(ed)	/* End delete mode */
#define EI		TCSTR(ei)	/* End insert mode */
#define cursor_home	TCSTR(ho)	/* Home cursor */
#define insert_character TCSTR(ic)	/* Insert character */
#define IM		TCSTR(im)	/* Insert mode (enter) */
#define IS		TCSTR(is)	/* Terminal initialization string */
#define LL		TCSTR(ll)	/* Last line, first column */
#define cursor_right	TCSTR(nd)	/* Non-destructive space (cursor right) */
#if 0	/* Broken? */
# define fresh_line	TCSTR(nw)	/* Move to start of fresh line */
#endif
#define PC		TCSTR(pc)	/* Pad character */
#define SF		TCSTR(sf)	/* Scroll forward */
#define SR		TCSTR(sr)	/* Scroll reverse */
#define TA		TCSTR(ta)	/* Tab (other than '^I' or with padding) */
#define TE		TCSTR(te)	/* String to end programs that use CM */
#define TI		TCSTR(ti)	/* String to begin programs that use CM */
#define cursor_up	TCSTR(up)	/* Upline (cursor up) */
#define flash_screen	TCSTR(vb)	/* Visible bell */
#define VE		TCSTR(ve)	/* Sequence to end open/visual mode */
#define VS		TCSTR(vs)	/* Sequence to start open/visual mode */
#define exit_standout_mode  TCSTR(se)	/* End standout mode */
#define enter_standout_mode TCSTR(so)	/* Begin standout mode */
#define parm_dch	TCSTR(DC)	/* Delete n characters */
#define parm_ich	TCSTR(IC)	/* Insert n characters */
#define parm_delete_line TCSTR(DL)	/* Delete n lines */
#define parm_insert_line TCSTR(AL)	/* Insert n lines */

#define columns TCNUM(co)	/* co Number of columns in a line */
#define BSdelay TCNUM(dB)	/* dB Backspace delay */
#define CRdelay TCNUM(dC)	/* dC Carriage return delay */
#define NLdelay TCNUM(dN)	/* dN Newline delay */
#define TAdelay TCNUM(dT)	/* dT Tab delay  */
#define lines   TCNUM(li)	/* li Number of lines on screen */


/* Things defined to look more like TERMINFO */
#define	over_strike	0
#define	bell		0

#ifndef macintosh
#include <windows.h>
#endif /* not macintosh */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "putty.h"

#define cfg		(s->cfg)
#define back		(s->back)
#define rows		(s->rows)
#define cols		(s->cols)
#define savelines	(s->savelines)
#define font_width	(s->font_width)
#define font_height	(s->font_height)
#define has_focus	(s->has_focus)
#define inbuf		(s->inbuf)
#define inbuf_head	(s->inbuf_head)
#define inbuf_reap	(s->inbuf_reap)
#define app_cursor_keys	(s->app_cursor_keys)
#define app_keypad_keys	(s->app_keypad_keys)
#define attr_mask	(s->attr_mask)

#define text		s->ts.text
#define scrtop		s->ts.scrtop
#define disptop		s->ts.disptop
#define sbtop		s->ts.sbtop
#define cpos		s->ts.cpos
#define disptext	s->ts.disptext
#define wanttext	s->ts.wanttext
#define alttext		s->ts.alttext

#define selspace	s->ts.selspace

#define TSIZE (sizeof(*text))
#define fix_cpos  do { cpos = scrtop + curs_y * (cols+1) + curs_x; } while(0)

#define curr_attr	s->ts.curr_attr
#define save_attr	s->ts.save_attr

#define curs_x		s->ts.curs_x
#define curs_y		s->ts.curs_y
#define save_x		s->ts.save_x
#define save_y		s->ts.save_y
#define marg_t		s->ts.marg_t
#define marg_b		s->ts.marg_b
#define curr_dec_om    	s->ts.dec_om
#define wrap		s->ts.wrap
#define wrapnext	s->ts.wrapnext
#define insert		s->ts.insert
#define cset		s->ts.cset
#define save_cset	s->ts.save_cset
#define save_csattr	s->ts.save_csattr
#define rvideo		s->ts.rvideo
#define cset_attr	s->ts.cset_attr

#define alt_x		s->ts.alt_x
#define alt_y		s->ts.alt_y
#define alt_om		s->ts.alt_om
#define alt_wrap	s->ts.alt_wrap
#define alt_wnext	s->ts.alt_wnext
#define alt_ins		s->ts.alt_ins
#define alt_cset	s->ts.alt_cset
#define alt_t		s->ts.alt_t
#define alt_b		s->ts.alt_b
#define alt_which	s->ts.alt_which

#define ARG_DEFAULT -1		       /* if an arg isn't specified */
#define def(a,d) ( (a) == ARG_DEFAULT ? (d) : (a) )

#define esc_args	s->ts.esc_args
#define esc_nargs	s->ts.esc_nargs
#define esc_query	s->ts.esc_query

#define osc_strlen	s->ts.osc_strlen
#define osc_string	s->ts.osc_string
#define osc_w		s->ts.osc_w

#define tabs		s->ts.tabs

#define MAXNL 5
#define nl_count	s->ts.nl_count

#define termstate	(s->ts.termstate)
#define selstate	(s->ts.selstate)
#define selmode		(s->ts.selmode)
#define selstart	(s->ts.selstart)
#define selend		(s->ts.selend)
#define selanchor	(s->ts.selanchor)

#define curr_wordness	s->ts.wordness

static unsigned char sel_nl[] = SEL_NL;

/*
 * Internal prototypes.
 */
static void do_paint (Session *, int);
static void erase_lots (Session *, int, int, int);
static void swap_screen (Session *, int);
static void update_sbar (Session *);
static void deselect (Session *);
static void scroll_display(Session *, int, int, int);

/*
 * Set up power-on settings for the terminal.
 */
static void power_on(Session *s) {
    curs_x = curs_y = alt_x = alt_y = save_x = save_y = 0;
    alt_t = marg_t = 0;
    if (rows != -1)
	alt_b = marg_b = rows - 1;
    else
	alt_b = marg_b = 0;
    if (cols != -1) {
	int i;
	for (i = 0; i < cols; i++)
	    tabs[i] = (i % 8 == 0 ? TRUE : FALSE);
    }
    alt_om = curr_dec_om = cfg.dec_om;
    alt_wnext = wrapnext = alt_ins = insert = FALSE;
    alt_wrap = wrap = cfg.wrap_mode;
    alt_cset = cset = 0;
    cset_attr[0] = cset_attr[1] = ATTR_ASCII;
    rvideo = 0;
    save_attr = curr_attr = ATTR_DEFAULT;
    app_cursor_keys = cfg.app_cursor;
    app_keypad_keys = cfg.app_keypad;
    alt_which = 0;
    {
	int i;
	for (i = 0; i < 256; i++)
	    curr_wordness[i] = cfg.wordness[i];
    }
    if (text) {
	swap_screen (s, 1);
	erase_lots (s, FALSE, TRUE, TRUE);
	swap_screen (s, 0);
	erase_lots (s, FALSE, TRUE, TRUE);
    }
}

/*
 * Force a screen update.
 */
void term_update(Session *s) {

    pre_paint(s);
    do_paint(s, TRUE);
    post_paint(s);
    nl_count = 0;
}

/*
 * Same as power_on(), but an external function.
 */
void term_pwron(Session *s) {
    power_on(s);
    fix_cpos;
    disptop = scrtop;
    deselect(s);
    term_update(s);
}

/*
 * Clear the scrollback.
 */
void term_clrsb(Session *s) {
    disptop = sbtop = scrtop;
    update_sbar(s);
}

/*
 * Initialise the terminal.
 */
void term_init(Session *s) {
    text = sbtop = scrtop = disptop = cpos = NULL;
    disptext = wanttext = NULL;
    tabs = NULL;
    selspace = NULL;
    deselect(s);
    rows = cols = -1;
    nl_count = 0;
    power_on(s);
}

/*
 * Set up the terminal for a given size.
 */
void term_size(Session *s, int newrows, int newcols, int newsavelines) {
    unsigned long *newtext, *newdisp, *newwant, *newalt;
    int i, j, crows, ccols;

    if (newrows == rows && newcols == cols && newsavelines == savelines)
	return;			       /* nothing to do */

    alt_t = marg_t = 0;
    alt_b = marg_b = newrows - 1;

    newtext = smalloc ((newrows+newsavelines)*(newcols+1)*TSIZE);
    disptop = newtext + newsavelines*(newcols+1);
    for (i=0; i<(newrows+newsavelines)*(newcols+1); i++)
	newtext[i] = ERASE_CHAR;
    if (rows != -1) {
	crows = rows + (scrtop - sbtop) / (cols+1);
	if (crows > newrows+newsavelines)
	    crows = newrows+newsavelines;
	ccols = (cols < newcols ? cols : newcols);
	for (i=0; i<crows; i++) {
	    int oldidx = (rows + savelines - crows + i) * (cols+1);
	    int newidx = (newrows + newsavelines - crows + i) * (newcols+1);
	    for (j=0; j<ccols; j++)
		newtext[newidx+j] = text[oldidx+j];
	    newtext[newidx+newcols] =
		(cols == newcols ? text[oldidx+cols] : 0);
	}
	sbtop = disptop - (crows - newrows) * (newcols+1);
	if (sbtop > disptop)
	    sbtop = disptop;
    } else
	sbtop = disptop;
    scrtop = disptop;
    sfree (text);
    text = newtext;

    newdisp = smalloc (newrows*(newcols+1)*TSIZE);
    for (i=0; i<newrows*(newcols+1); i++)
	newdisp[i] = ATTR_INVALID;
    sfree (disptext);
    disptext = newdisp;

    newwant = smalloc (newrows*(newcols+1)*TSIZE);
    for (i=0; i<newrows*(newcols+1); i++)
	newwant[i] = ATTR_INVALID;
    sfree (wanttext);
    wanttext = newwant;

    newalt = smalloc (newrows*(newcols+1)*TSIZE);
    for (i=0; i<newrows*(newcols+1); i++)
	newalt[i] = ERASE_CHAR;
    sfree (alttext);
    alttext = newalt;

    sfree (selspace);
    selspace = smalloc ( (newrows+newsavelines) * (newcols+sizeof(sel_nl)) );

    tabs = srealloc (tabs, newcols*sizeof(*tabs));
    {
	int i;
	for (i = (cols > 0 ? cols : 0); i < newcols; i++)
	    tabs[i] = (i % 8 == 0 ? TRUE : FALSE);
    }

    if (rows > 0)
	curs_y += newrows - rows;
    if (curs_y < 0)
	curs_y = 0;
    if (curs_y >= newrows)
	curs_y = newrows-1;
    if (curs_x >= newcols)
	curs_x = newcols-1;
    alt_x = alt_y = 0;
    wrapnext = alt_wnext = FALSE;

    rows = newrows;
    cols = newcols;
    savelines = newsavelines;
    fix_cpos;

    deselect(s);
    update_sbar(s);
    term_update(s);
}

/*
 * Swap screens.
 */
static void swap_screen (Session *s, int which) {
    int t;
    unsigned long tt;

    if (which == alt_which)
	return;

    alt_which = which;

    for (t=0; t<rows*(cols+1); t++) {
	tt = scrtop[t]; scrtop[t] = alttext[t]; alttext[t] = tt;
    }

    t = curs_x; curs_x = alt_x; alt_x = t;
    t = curs_y; curs_y = alt_y; alt_y = t;
    t = marg_t; marg_t = alt_t; alt_t = t;
    t = marg_b; marg_b = alt_b; alt_b = t;
    t = curr_dec_om; curr_dec_om = alt_om; alt_om = t;
    t = wrap; wrap = alt_wrap; alt_wrap = t;
    t = wrapnext; wrapnext = alt_wnext; alt_wnext = t;
    t = insert; insert = alt_ins; alt_ins = t;
    t = cset; cset = alt_cset; alt_cset = t;

    fix_cpos;
}

/*
 * Retrieve a character from `inbuf'.
 */
static int inbuf_getc(Session *s) {
    if (inbuf_head == inbuf_reap)
	return -1;		       /* EOF */
    else {
	int n = inbuf_reap;
	inbuf_reap = (inbuf_reap+1) & INBUF_MASK;
	return inbuf[n];
    }
}

/*
 * Update the scroll bar.
 */
static void update_sbar(Session *s) {
    int min;

    min = (sbtop - text) / (cols+1);
    set_sbar(s, (scrtop - text) / (cols+1) + rows - min,
	     (disptop - text) / (cols+1) - min,
	     rows);
}

/*
 * Check whether the region bounded by the two pointers intersects
 * the selection, and de-select the on-screen selection if so.
 */
static void check_selection(Session *s,
			    unsigned long *from, unsigned long *to) {
    if (from < selend && selstart < to)
	deselect(s);
}

/*
 * Scroll the screen. (`lines' is +ve for scrolling forward, -ve
 * for backward.) `sb' is TRUE if the scrolling is permitted to
 * affect the scrollback buffer.
 */
static void scroll (Session *s, int topline, int botline, int lines, int sb) {
    unsigned long *scroll_top;
    int scroll_size, size, i;

    scroll_top = scrtop + topline*(cols+1);
    size = (lines < 0 ? -lines : lines) * (cols+1);
    scroll_size = (botline - topline + 1) * (cols+1) - size;

    if (lines > 0 && topline == 0 && botline == (rows-1) && sb) {
	/*
	 * Since we're going to scroll the whole screen upwards,
	 * let's also affect the scrollback buffer.
	 */
	sbtop -= lines * (cols+1);
	if (sbtop < text)
	    sbtop = text;
	scroll_size += scroll_top - sbtop;
	scroll_top = sbtop;
	update_sbar(s);
    }

    if (scroll_size < 0) {
	size += scroll_size;
	scroll_size = 0;
    }

    if (lines > 0) {
	if (scroll_size)
	    memmove (scroll_top, scroll_top + size, scroll_size*TSIZE);
	for (i = 0; i < size; i++)
	    scroll_top[i+scroll_size] = ERASE_CHAR;
	if (selstart > scroll_top &&
	    selstart < scroll_top + size + scroll_size) {
	    selstart -= size;
	    if (selstart < scroll_top)
		selstart = scroll_top;
	}
	if (selend > scroll_top &&
	    selend < scroll_top + size + scroll_size) {
	    selend -= size;
	    if (selend < scroll_top)
		selend = scroll_top;
	}
    } else {
	if (scroll_size)
	    memmove (scroll_top + size, scroll_top, scroll_size*TSIZE);
	for (i = 0; i < size; i++)
	    scroll_top[i] = ERASE_CHAR;
	if (selstart > scroll_top &&
	    selstart < scroll_top + size + scroll_size) {
	    selstart += size;
	    if (selstart > scroll_top + size + scroll_size)
		selstart = scroll_top + size + scroll_size;
	}
	if (selend > scroll_top &&
	    selend < scroll_top + size + scroll_size) {
	    selend += size;
	    if (selend > scroll_top + size + scroll_size)
		selend = scroll_top + size + scroll_size;
	}
    }
#ifdef OPTIMISE_SCROLL
    scroll_display(s, topline, botline, lines);
#endif
}

#ifdef OPTIMISE_SCROLL
static void scroll_display(Session *s, int topline, int botline, int lines) {
    unsigned long *start, *end;
    int distance, size, i;

    start = disptext + topline * (cols + 1);
    end = disptext + (botline + 1) * (cols + 1);
    distance = (lines > 0 ? lines : -lines) * (cols + 1);
    size = end - start - distance;
    if (lines > 0) {
	memmove(start, start + distance, size * TSIZE);
	for (i = 0; i < distance; i++)
	    (start + size)[i] |= ATTR_INVALID;
    } else {
	memmove(start + distance, start, size * TSIZE);
	for (i = 0; i < distance; i++)
	    start[i] |= ATTR_INVALID;
    }
    do_scroll(s, topline, botline, lines);
}
#endif /* OPTIMISE_SCROLL */
    

/*
 * Move the cursor to a given position, clipping at boundaries. We
 * may or may not want to clip at the scroll margin: marg_clip is 0
 * not to, 1 to disallow _passing_ the margins, and 2 to disallow
 * even _being_ outside the margins.
 */
static void move (Session *s, int x, int y, int marg_clip) {
    if (x < 0)
	x = 0;
    if (x >= cols)
	x = cols-1;
    if (marg_clip) {
	if ((curs_y >= marg_t || marg_clip == 2) && y < marg_t)
	    y = marg_t;
	if ((curs_y <= marg_b || marg_clip == 2) && y > marg_b)
	    y = marg_b;
    }
    if (y < 0)
	y = 0;
    if (y >= rows)
	y = rows-1;
    curs_x = x;
    curs_y = y;
    fix_cpos;
    wrapnext = FALSE;
}

/*
 * Save or restore the cursor and SGR mode.
 */
static void save_cursor(Session *s, int save) {
    if (save) {
	save_x = curs_x;
	save_y = curs_y;
	save_attr = curr_attr;
	save_cset = cset;
	save_csattr = cset_attr[cset];
    } else {
	curs_x = save_x;
	curs_y = save_y;
	curr_attr = save_attr;
	cset = save_cset;
	cset_attr[cset] = save_csattr;
	fix_cpos;
    }
}

/*
 * Erase a large portion of the screen: the whole screen, or the
 * whole line, or parts thereof.
 */
static void erase_lots(Session *s, int line_only, int from_begin, int to_end) {
    unsigned long *startpos, *endpos;

    if (line_only) {
	startpos = cpos - curs_x;
	endpos = startpos + cols+1;
    } else {
	startpos = scrtop;
	endpos = startpos + rows * (cols+1);
    }
    if (!from_begin)
	startpos = cpos;
    if (!to_end)
	endpos = cpos;
    check_selection(s, startpos, endpos);
    while (startpos < endpos)
	*startpos++ = ERASE_CHAR;
}

/*
 * Insert or delete characters within the current line. n is +ve if
 * insertion is desired, and -ve for deletion.
 */
static void insch(Session *s, int n) {
    int dir = (n < 0 ? -1 : +1);
    int m;

    n = (n < 0 ? -n : n);
    if (n > cols - curs_x)
	n = cols - curs_x;
    m = cols - curs_x - n;
    check_selection(s, cpos, cpos+n);
    if (dir < 0) {
	memmove (cpos, cpos+n, m*TSIZE);
	while (n--)
	    cpos[m++] = ERASE_CHAR;
    } else {
	memmove (cpos+n, cpos, m*TSIZE);
	while (n--)
	    cpos[n] = ERASE_CHAR;
    }
}

/*
 * Toggle terminal mode `mode' to state `state'. (`query' indicates
 * whether the mode is a DEC private one or a normal one.)
 */
static void toggle_mode(Session *s, int mode, int query, int state) {
    if (query) switch (mode) {
      case 1:			       /* application cursor keys */
	app_cursor_keys = state;
	break;
      case 3:			       /* 80/132 columns */
	deselect(s);
	request_resize(s, state ? 132 : 80, rows);
	break;
      case 5:			       /* reverse video */
	rvideo = state;
	disptop = scrtop;
	break;
      case 6:			       /* DEC origin mode */
	curr_dec_om = state;
	break;
      case 7:			       /* auto wrap */
	wrap = state;
	break;
      case 47:			       /* alternate screen */
	deselect(s);
	swap_screen(s, state);
	disptop = scrtop;
	break;
    } else switch (mode) {
      case 4:			       /* set insert mode */
	insert = state;
	break;
    }
}

/*
 * Process an OSC sequence: set window title or icon name.
 */
static void do_osc(Session *s) {
    if (osc_w) {
	while (osc_strlen--)
	    curr_wordness[(unsigned char)osc_string[osc_strlen]] = esc_args[0];
    } else {
	osc_string[osc_strlen] = '\0';
	switch (esc_args[0]) {
	  case 0:
	  case 1:
	    set_icon (s, osc_string);
	    if (esc_args[0] == 1)
		break;
	    /* fall through: parameter 0 means set both */
	  case 2:
	  case 21:
	    set_title(s, osc_string);
	    break;
	}
    }
}

/*
 * Remove everything currently in `inbuf' and stick it up on the
 * in-memory display. There's a big state machine in here to
 * process escape sequences...
 */
void term_out(Session *s) {
    int c;
    int must_update = FALSE;

    while ( (c = inbuf_getc(s)) != -1) {
#ifdef LOG
	{
	    static FILE *fp = NULL;
	    if (!fp) fp = fopen("putty.log", "wb");
	    if (fp) fputc (c, fp);
	}
#endif
	switch (termstate) {
	  case TOPLEVEL:
	    do_toplevel:
	    switch (c) {
	      case '\005':	       /* terminal type query */
		back->send(s, "\033[?1;2c", 7);
		break;
	      case '\007':
		beep(s);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case '\b':
		if (curs_x == 0 && curs_y > 0)
		    curs_x = cols-1, curs_y--;
		else if (wrapnext)
		    wrapnext = FALSE;
		else
		    curs_x--;
		fix_cpos;
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case '\016':
		cset = 1;
		break;
	      case '\017':
		cset = 0;
		break;
	      case '\033':
		termstate = SEEN_ESC;
		break;
	      case 0233:
		termstate = SEEN_CSI;
		esc_nargs = 1;
		esc_args[0] = ARG_DEFAULT;
		esc_query = FALSE;
		break;
	      case 0235:
		termstate = SEEN_OSC;
		esc_args[0] = 0;
		break;
	      case '\015':
		curs_x = 0;
		wrapnext = FALSE;
		fix_cpos;
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case '\013':
	      case '\014':
	      case '\012':
		if (curs_y == marg_b)
		    scroll(s, marg_t, marg_b, 1, TRUE);
		else if (curs_y < rows-1)
		    curs_y++;
                if (cfg.lfhascr)
                    curs_x = 0;
		fix_cpos;
		wrapnext = FALSE;
		disptop = scrtop;
		nl_count++;
		break;
	      case '\t':
		do {
		    curs_x++;
		} while (curs_x < cols-1 && !tabs[curs_x]);
		if (curs_x >= cols)
		    curs_x = cols-1;
		{
		    unsigned long *old_cpos = cpos;
		    fix_cpos;
		    check_selection(s, old_cpos, cpos);
		}
		disptop = scrtop;
		must_update = TRUE;
		break;
	      default:
		if (c >= ' ' && c != 0234) {
		    if (wrapnext) {
			cpos[1] = ATTR_WRAPPED;
			if (curs_y == marg_b)
			    scroll(s, marg_t, marg_b, 1, TRUE);
			else if (curs_y < rows-1)
			    curs_y++;
			curs_x = 0;
			fix_cpos;
			wrapnext = FALSE;
			nl_count++;
		    }
		    if (insert)
			insch(s, 1);
		    check_selection(s, cpos, cpos+1);
		    *cpos++ = c | curr_attr | 
			(c <= 0x7F ? cset_attr[cset] : ATTR_ASCII);
		    curs_x++;
		    if (curs_x == cols) {
			cpos--;
			curs_x--;
			wrapnext = wrap;
		    }
		    disptop = scrtop;
		}
	    }
	    break;
	  case IGNORE_NEXT:
	    termstate = TOPLEVEL;
	    break;
	  case OSC_MAYBE_ST:
	    /*
	     * This state is virtually identical to SEEN_ESC, with the
	     * exception that we have an OSC sequence in the pipeline,
	     * and _if_ we see a backslash, we process it.
	     */
	    if (c == '\\') {
		do_osc(s);
		termstate = TOPLEVEL;
		break;
	    }
	    /* else fall through */
	  case SEEN_ESC:
	    termstate = TOPLEVEL;
	    switch (c) {
	      case '\005': case '\007': case '\b': case '\016': case '\017':
	      case '\033': case 0233: case 0234: case 0235: case '\015':
	      case '\013': case '\014': case '\012': case '\t':
		termstate = TOPLEVEL;
		goto do_toplevel;      /* hack... */
	      case ' ':		       /* some weird sequence? */
		termstate = IGNORE_NEXT;
		break;
	      case '[':		       /* enter CSI mode */
		termstate = SEEN_CSI;
		esc_nargs = 1;
		esc_args[0] = ARG_DEFAULT;
		esc_query = FALSE;
		break;
	      case ']':		       /* xterm escape sequences */
		termstate = SEEN_OSC;
		esc_args[0] = 0;
		break;
	      case '(':		       /* should set GL */
		termstate = SET_GL;
		break;
	      case ')':		       /* should set GR */
		termstate = SET_GR;
		break;
	      case '7':		       /* save cursor */
		save_cursor(s, TRUE);
		break;
	      case '8':		       /* restore cursor */
		save_cursor(s, FALSE);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case '=':
		app_keypad_keys = TRUE;
		break;
	      case '>':
		app_keypad_keys = FALSE;
		break;
	      case 'D':		       /* exactly equivalent to LF */
		if (curs_y == marg_b)
		    scroll(s, marg_t, marg_b, 1, TRUE);
		else if (curs_y < rows-1)
		    curs_y++;
		fix_cpos;
		wrapnext = FALSE;
		disptop = scrtop;
		nl_count++;
		break;
	      case 'E':		       /* exactly equivalent to CR-LF */
		curs_x = 0;
		wrapnext = FALSE;
		if (curs_y == marg_b)
		    scroll(s, marg_t, marg_b, 1, TRUE);
		else if (curs_y < rows-1)
		    curs_y++;
		fix_cpos;
		wrapnext = FALSE;
		nl_count++;
		disptop = scrtop;
		break;
	      case 'M':		       /* reverse index - backwards LF */
		if (curs_y == marg_t)
		    scroll(s, marg_t, marg_b, -1, TRUE);
		else if (curs_y > 0)
		    curs_y--;
		fix_cpos;
		wrapnext = FALSE;
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'Z':		       /* terminal type query */
		back->send(s, "\033[?6c", 5);
		break;
	      case 'c':		       /* restore power-on settings */
		power_on(s);
		fix_cpos;
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case '#':		       /* ESC # 8 fills screen with Es :-) */
		termstate = SEEN_ESCHASH;
		break;
	      case 'H':		       /* set a tab */
		tabs[curs_x] = TRUE;
		break;
	    }
	    break;
	  case SEEN_CSI:
	    termstate = TOPLEVEL;      /* default */
	    switch (c) {
	      case '\005': case '\007': case '\b': case '\016': case '\017':
	      case '\033': case 0233: case 0234: case 0235: case '\015':
	      case '\013': case '\014': case '\012': case '\t':
		termstate = TOPLEVEL;
		goto do_toplevel;      /* hack... */
	      case '0': case '1': case '2': case '3': case '4':
	      case '5': case '6': case '7': case '8': case '9':
		if (esc_nargs <= ARGS_MAX) {
		    if (esc_args[esc_nargs-1] == ARG_DEFAULT)
			esc_args[esc_nargs-1] = 0;
		    esc_args[esc_nargs-1] =
			10 * esc_args[esc_nargs-1] + c - '0';
		}
		termstate = SEEN_CSI;
		break;
	      case ';':
		if (++esc_nargs <= ARGS_MAX)
		    esc_args[esc_nargs-1] = ARG_DEFAULT;
		termstate = SEEN_CSI;
		break;
	      case '?':
		esc_query = TRUE;
		termstate = SEEN_CSI;
		break;
	      case 'A':		       /* move up N lines */
		move(s, curs_x, curs_y - def(esc_args[0], 1), 1);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'B': case 'e':      /* move down N lines */
		move(s, curs_x, curs_y + def(esc_args[0], 1), 1);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'C': case 'a':      /* move right N cols */
		move(s, curs_x + def(esc_args[0], 1), curs_y, 1);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'D':		       /* move left N cols */
		move(s, curs_x - def(esc_args[0], 1), curs_y, 1);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'E':		       /* move down N lines and CR */
		move(s, 0, curs_y + def(esc_args[0], 1), 1);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'F':		       /* move up N lines and CR */
		move(s, 0, curs_y - def(esc_args[0], 1), 1);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'G': case '`':      /* set horizontal posn */
		move(s, def(esc_args[0], 1) - 1, curs_y, 0);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'd':		       /* set vertical posn */
		move(s, curs_x, (curr_dec_om ? marg_t : 0) + def(esc_args[0], 1) - 1,
		      (curr_dec_om ? 2 : 0));
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'H': case 'f':      /* set horz and vert posns at once */
		if (esc_nargs < 2)
		    esc_args[1] = ARG_DEFAULT;
		move(s, def(esc_args[1], 1) - 1,
		     (curr_dec_om ? marg_t : 0) + def(esc_args[0], 1) - 1,
		     (curr_dec_om ? 2 : 0));
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'J':		       /* erase screen or parts of it */
		{
		    unsigned int i = def(esc_args[0], 0) + 1;
		    if (i > 3)
			i = 0;
		    erase_lots(s, FALSE, !!(i & 2), !!(i & 1));
		}
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'K':		       /* erase line or parts of it */
		{
		    unsigned int i = def(esc_args[0], 0) + 1;
		    if (i > 3)
			i = 0;
		    erase_lots(s, TRUE, !!(i & 2), !!(i & 1));
		}
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'L':		       /* insert lines */
		if (curs_y <= marg_b)
		    scroll(s, curs_y, marg_b, -def(esc_args[0], 1), FALSE);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'M':		       /* delete lines */
		if (curs_y <= marg_b)
		    scroll(s, curs_y, marg_b, def(esc_args[0], 1), FALSE);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case '@':		       /* insert chars */
		insch(s, def(esc_args[0], 1));
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'P':		       /* delete chars */
		insch(s, -def(esc_args[0], 1));
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 'c':		       /* terminal type query */
		back->send(s, "\033[?6c", 5);
		break;
	      case 'n':		       /* cursor position query */
		if (esc_args[0] == 6) {
		    char buf[32];
		    sprintf (buf, "\033[%d;%dR", curs_y + 1, curs_x + 1);
		    back->send(s, buf, strlen(buf));
		}
		break;
	      case 'h':		       /* toggle a mode to high */
		toggle_mode(s, esc_args[0], esc_query, TRUE);
		break;
	      case 'l':		       /* toggle a mode to low */
		toggle_mode(s, esc_args[0], esc_query, FALSE);
		break;
	      case 'g':		       /* clear tabs */
		if (esc_nargs == 1) {
		    if (esc_args[0] == 0) {
			tabs[curs_x] = FALSE;
		    } else if (esc_args[0] == 3) {
			int i;
			for (i = 0; i < cols; i++)
			    tabs[i] = FALSE;
		    }
		}
		break;
	      case 'r':		       /* set scroll margins */
		if (esc_nargs <= 2) {
		    int top, bot;
		    top = def(esc_args[0], 1) - 1;
		    if (top < 0)
			top = 0;
		    bot = (esc_nargs <= 1 || esc_args[1] == 0 ? rows :
			   def(esc_args[1], rows)) - 1;
		    if (bot >= rows)
			bot = rows-1;
		    if (top <= bot) {
			marg_t = top;
			marg_b = bot;
			curs_x = 0;
			/*
			 * I used to think the cursor should be
			 * placed at the top of the newly marginned
			 * area. Apparently not: VMS TPU falls over
			 * if so.
			 */
			curs_y = 0;
			fix_cpos;
			disptop = scrtop;
			must_update = TRUE;
		    }
		}
		break;
	      case 'm':		       /* set graphics rendition */
		{
		    int i;
		    for (i=0; i<esc_nargs; i++) {
			switch (def(esc_args[i], 0)) {
			  case 0:      /* restore defaults */
			    curr_attr = ATTR_DEFAULT; break;
			  case 1:      /* enable bold */
			    curr_attr |= ATTR_BOLD; break;
			  case 4:      /* enable underline */
			  case 21:     /* (enable double underline) */
			    curr_attr |= ATTR_UNDER; break;
			  case 7:      /* enable reverse video */
			    curr_attr |= ATTR_REVERSE; break;
			  case 22:     /* disable bold */
			    curr_attr &= ~ATTR_BOLD; break;
			  case 24:     /* disable underline */
			    curr_attr &= ~ATTR_UNDER; break;
			  case 27:     /* disable reverse video */
			    curr_attr &= ~ATTR_REVERSE; break;
			  case 30: case 31: case 32: case 33:
			  case 34: case 35: case 36: case 37:
			    /* foreground */
			    curr_attr &= ~ATTR_FGMASK;
			    curr_attr |= (esc_args[i] - 30) << ATTR_FGSHIFT;
			    break;
			  case 39:     /* default-foreground */
			    curr_attr &= ~ATTR_FGMASK;
			    curr_attr |= ATTR_DEFFG;
			    break;
			  case 40: case 41: case 42: case 43:
			  case 44: case 45: case 46: case 47:
			    /* background */
			    curr_attr &= ~ATTR_BGMASK;
			    curr_attr |= (esc_args[i] - 40) << ATTR_BGSHIFT;
			    break;
			  case 49:     /* default-background */
			    curr_attr &= ~ATTR_BGMASK;
			    curr_attr |= ATTR_DEFBG;
			    break;
			}
		    }
		}
		break;
	      case 's':		       /* save cursor */
		save_cursor(s, TRUE);
		break;
	      case 'u':		       /* restore cursor */
		save_cursor(s, FALSE);
		disptop = scrtop;
		must_update = TRUE;
		break;
	      case 't':		       /* set page size - ie window height */
		request_resize(s, cols, def(esc_args[0], 24));
		deselect(s);
		break;
	      case 'X':		       /* write N spaces w/o moving cursor */
		{
		    int n = def(esc_args[0], 1);
		    unsigned long *p = cpos;
		    if (n > cols - curs_x)
			n = cols - curs_x;
		    check_selection(s, cpos, cpos+n);
		    while (n--)
			*p++ = ERASE_CHAR;
		    disptop = scrtop;
		    must_update = TRUE;
		}
		break;
	      case 'x':		       /* report terminal characteristics */
		{
		    char buf[32];
		    int i = def(esc_args[0], 0);
		    if (i == 0 || i == 1) {
			strcpy (buf, "\033[2;1;1;112;112;1;0x");
			buf[2] += i;
			back->send(s, buf, 20);
		    }
		}
		break;
	    }
	    break;
	  case SET_GL:
	  case SET_GR:
	    switch (c) {
	      case 'A':
		cset_attr[termstate == SET_GL ? 0 : 1] = ATTR_GBCHR;
		break;
	      case '0':
		cset_attr[termstate == SET_GL ? 0 : 1] = ATTR_LINEDRW;
		break;
	      default:		       /* specifically, 'B' */
		cset_attr[termstate == SET_GL ? 0 : 1] = ATTR_ASCII;
		break;
	    }
	    termstate = TOPLEVEL;
	    break;
	  case SEEN_OSC:
	    osc_w = FALSE;
	    switch (c) {
	      case '\005': case '\007': case '\b': case '\016': case '\017':
	      case '\033': case 0233: case 0234: case 0235: case '\015':
	      case '\013': case '\014': case '\012': case '\t':
		termstate = TOPLEVEL;
		goto do_toplevel;      /* hack... */
	      case 'P':		       /* Linux palette sequence */
		termstate = SEEN_OSC_P;
		osc_strlen = 0;
		break;
	      case 'R':		       /* Linux palette reset */
		palette_reset(s);
		term_invalidate(s);
		termstate = TOPLEVEL;
		break;
	      case 'W':		       /* word-set */
		termstate = SEEN_OSC_W;
		osc_w = TRUE;
		break;
	      case '0': case '1': case '2': case '3': case '4':
	      case '5': case '6': case '7': case '8': case '9':
		esc_args[0] = 10 * esc_args[0] + c - '0';
		break;
	      case 'L':
		/*
		 * Grotty hack to support xterm and DECterm title
		 * sequences concurrently.
		 */
		if (esc_args[0] == 2) {
		    esc_args[0] = 1;
		    break;
		}
		/* else fall through */
	      default:
		termstate = OSC_STRING;
		osc_strlen = 0;
	    }
	    break;
	  case OSC_STRING:
	    if (c == 0234 || c == '\007') {
		/*
		 * These characters terminate the string; ST and BEL
		 * terminate the sequence and trigger instant
		 * processing of it, whereas ESC goes back to SEEN_ESC
		 * mode unless it is followed by \, in which case it is
		 * synonymous with ST in the first place.
		 */
		do_osc(s);
		termstate = TOPLEVEL;
	    } else if (c == '\033')
		    termstate = OSC_MAYBE_ST;
	    else if (osc_strlen < OSC_STR_MAX)
		osc_string[osc_strlen++] = c;
	    break;
	  case SEEN_OSC_P:
	    {
		int max = (osc_strlen == 0 ? 21 : 16);
		int val;
		if (c >= '0' && c <= '9')
		    val = c - '0';
		else if (c >= 'A' && c <= 'A'+max-10)
		    val = c - 'A' + 10;
		else if (c >= 'a' && c <= 'a'+max-10)
		    val = c - 'a' + 10;
		else
		    termstate = TOPLEVEL;
		osc_string[osc_strlen++] = val;
		if (osc_strlen >= 7) {
		    palette_set (s, osc_string[0],
				 osc_string[1] * 16 + osc_string[2],
				 osc_string[3] * 16 + osc_string[4],
				 osc_string[5] * 16 + osc_string[6]);
		    termstate = TOPLEVEL;
		}
	    }
	    break;
	  case SEEN_OSC_W:
	    switch (c) {
	      case '\005': case '\007': case '\b': case '\016': case '\017':
	      case '\033': case 0233: case 0234: case 0235: case '\015':
	      case '\013': case '\014': case '\012': case '\t':
		termstate = TOPLEVEL;
		goto do_toplevel;      /* hack... */
	      case '0': case '1': case '2': case '3': case '4':
	      case '5': case '6': case '7': case '8': case '9':
		esc_args[0] = 10 * esc_args[0] + c - '0';
		break;
	      default:
		termstate = OSC_STRING;
		osc_strlen = 0;
	    }
	    break;
	  case SEEN_ESCHASH:
	    if (c == '8') {
		unsigned long *p = scrtop;
		int n = rows * (cols+1);
		while (n--)
		    *p++ = ATTR_DEFAULT | 'E';
		disptop = scrtop;
		must_update = TRUE;
		check_selection(s, scrtop, scrtop + rows * (cols+1));
	    }
	    termstate = TOPLEVEL;
	    break;
	}
	check_selection(s, cpos, cpos+1);
    }
	
    if (must_update || nl_count > MAXNL) {
	update_sbar(s);
	term_update(s);
    }
}

/*
 * Given a context, update the window. Out of paranoia, we don't
 * allow WM_PAINT responses to do scrolling optimisations.
 */
static void do_paint (Session *s, int may_optimise){ 
    int i, j, start, our_curs_y;
    unsigned long attr, rv, cursor;
    char ch[1024];

    cursor = (has_focus ? ATTR_ACTCURS : ATTR_PASCURS);
    rv = (rvideo ? ATTR_REVERSE : 0);
    our_curs_y = curs_y + (scrtop - disptop) / (cols+1);

    for (i=0; i<rows; i++) {
	int idx = i*(cols+1);
	for (j=0; j<=cols; j++,idx++) {
	    unsigned long *d = disptop+idx;
	    wanttext[idx] = ((*d ^ rv
			      ^ (selstart <= d && d < selend ?
				 ATTR_REVERSE : 0)) |
			     (i==our_curs_y && j==curs_x ? cursor : 0));
	}
    }

    /*
     * We would perform scrolling optimisations in here, if they
     * didn't have a nasty tendency to cause the whole sodding
     * program to hang for a second at speed-critical moments.
     * We'll leave it well alone...
     */

    for (i=0; i<rows; i++) {
	int idx = i*(cols+1);
	start = -1;
	for (j=0; j<=cols; j++,idx++) {
	    unsigned long t = wanttext[idx];
	    int needs_update = (j < cols && t != disptext[idx]);
	    int keep_going = (start != -1 && needs_update &&
			      (t & attr_mask) == attr &&
			      j-start < sizeof(ch));
	    if (start != -1 && !keep_going) {
		do_text(s, start, i, ch, j-start, attr);
		start = -1;
	    }
	    if (needs_update) {
		if (start == -1) {
		    start = j;
		    attr = t & attr_mask;
		}
		ch[j-start] = (char) (t & CHAR_MASK);
	    }
	    disptext[idx] = t;
	}
    }
}

/*
 * Invalidate the whole screen so it will be repainted in full.
 */
void term_invalidate(Session *s) {
    int i;

    for (i=0; i<rows*(cols+1); i++)
	disptext[i] = ATTR_INVALID;
}

/*
 * Paint the window in response to a WM_PAINT message.
 */
void term_paint(Session *s, int l, int t, int r, int b) {
    int i, j, left, top, right, bottom;

    left = l / font_width;
    right = (r - 1) / font_width;
    top = t / font_height;
    bottom = (b - 1) / font_height;
    for (i = top; i <= bottom && i < rows ; i++)
      for (j = left; j <= right && j < cols ; j++)
	    disptext[i*(cols+1)+j] = ATTR_INVALID;

    do_paint (s, FALSE);
}

/*
 * Attempt to scroll the scrollback. The second parameter gives the
 * position we want to scroll to; the first is +1 to denote that
 * this position is relative to the beginning of the scrollback, -1
 * to denote it is relative to the end, and 0 to denote that it is
 * relative to the current position.
 */
void term_scroll(Session *s, int rel, int where) {
    int n = where * (cols+1);
#ifdef OPTIMISE_SCROLL
    unsigned long *olddisptop = disptop;
    int shift;
#endif /* OPTIMISE_SCROLL */

    disptop = (rel < 0 ? scrtop :
	       rel > 0 ? sbtop : disptop) + n;
    if (disptop < sbtop)
	disptop = sbtop;
    if (disptop > scrtop)
	disptop = scrtop;
    update_sbar(s);
#ifdef OPTIMISE_SCROLL
    shift = (disptop - olddisptop) / (cols + 1);
    if (shift < rows && shift > -rows)
	scroll_display(s, 0, rows - 1, shift);
#endif /* OPTIMISE_SCROLL */
    term_update(s);
}

/*
 * Spread the selection outwards according to the selection mode.
 */
static unsigned long *sel_spread_half(Session *s, unsigned long *p, int dir) {
    unsigned long *linestart, *lineend;
    int x;
    short wvalue;

    x = (p - text) % (cols+1);
    linestart = p - x;
    lineend = linestart + cols;

    switch (selmode) {
      case SM_CHAR:
	/*
	 * In this mode, every character is a separate unit, except
	 * for runs of spaces at the end of a non-wrapping line.
	 */
	if (!(linestart[cols] & ATTR_WRAPPED)) {
	    unsigned long *q = lineend;
	    while (q > linestart && (q[-1] & CHAR_MASK) == 0x20)
		q--;
	    if (q == lineend)
		q--;
	    if (p >= q)
		p = (dir == -1 ? q : lineend - 1);
	}
	break;
      case SM_WORD:
	/*
	 * In this mode, the units are maximal runs of characters
	 * whose `wordness' has the same value.
	 */
	wvalue = curr_wordness[*p & CHAR_MASK];
	if (dir == +1) {
	    while (p < lineend && curr_wordness[p[1] & CHAR_MASK] == wvalue)
		p++;
	} else {
	    while (p > linestart && curr_wordness[p[-1] & CHAR_MASK] == wvalue)
		p--;
	}
	break;
      case SM_LINE:
	/*
	 * In this mode, every line is a unit.
	 */
	p = (dir == -1 ? linestart : lineend - 1);
	break;
    }
    return p;
}

static void sel_spread(Session *s) {
    selstart = sel_spread_half(s, selstart, -1);
    selend = sel_spread_half(s, selend - 1, +1) + 1;
}

void term_mouse(Session *s, Mouse_Button b, Mouse_Action a, int x, int y) {
    unsigned long *selpoint;
    
    if (x < 0) {
	x = cols - 1;
	y--;
    } else if (x >= cols)
	x = cols - 1;

    selpoint = disptop + y * (cols + 1) + x;
    if (selpoint < sbtop)
	selpoint = sbtop;
    else if (selpoint > scrtop + rows * (cols + 1) - 1)
	/* XXX put this in a variable? */
	selpoint = scrtop + rows * (cols + 1) - 1;

    if (b == MB_SELECT && a == MA_CLICK) {
	deselect(s);
	selstate = ABOUT_TO;
	selanchor = selpoint;
	selmode = SM_CHAR;
    } else if (b == MB_SELECT && (a == MA_2CLK || a == MA_3CLK)) {
	deselect(s);
	selmode = (a == MA_2CLK ? SM_WORD : SM_LINE);
	selstate = DRAGGING;
	selstart = selanchor = selpoint;
	selend = selstart + 1;
	sel_spread(s);
    } else if ((b == MB_SELECT && a == MA_DRAG) ||
	       (b == MB_EXTEND && a != MA_RELEASE)) {
	if (selstate == ABOUT_TO && selanchor == selpoint)
	    return;
	if (b == MB_EXTEND && a != MA_DRAG && selstate == SELECTED) {
	    if (selpoint-selstart < (selend-selstart)/2)
		selanchor = selend - 1;
	    else
		selanchor = selstart;
	    selstate = DRAGGING;
	}
	if (selstate != ABOUT_TO && selstate != DRAGGING)
	    selanchor = selpoint;
	selstate = DRAGGING;
	if (selpoint < selanchor) {
	    selstart = selpoint;
	    selend = selanchor + 1;
	} else {
	    selstart = selanchor;
	    selend = selpoint + 1;
	}
	sel_spread(s);
    } else if ((b == MB_SELECT || b == MB_EXTEND) && a == MA_RELEASE)
	if (selstate == DRAGGING) {
	    if (cfg.implicit_copy)
		term_copy(s);
	    selstate = SELECTED;
	} else
	    selstate = NO_SELECTION;
    else if (b == MB_PASTE && (a==MA_CLICK || a==MA_2CLK || a==MA_3CLK))
	term_paste(s);
    term_update(s);
}

/*
 * We've completed a selection. We now transfer the
 * data to the clipboard.
 */
void term_copy(Session *s) {
    unsigned char *p = selspace;
    unsigned long *q = selstart;

    while (q < selend) {
	int nl = FALSE;
	unsigned long *lineend = q - (q-text) % (cols+1) + cols;
	unsigned long *nlpos = lineend;

	if (!(*nlpos & ATTR_WRAPPED)) {
	    while ((nlpos[-1] & CHAR_MASK) == 0x20 && nlpos > q)
		nlpos--;
	    if (nlpos < selend)
		nl = TRUE;
	}
	while (q < nlpos && q < selend)
	    *p++ = (unsigned char) (*q++ & CHAR_MASK);
	if (nl) {
	    int i;
	    for (i=0; i<sizeof(sel_nl); i++)
		*p++ = sel_nl[i];
	}
	q = lineend + 1;       /* start of next line */
    }
    write_clip(selspace, p - selspace);
}

void term_paste(Session *s) {
    char *data;
    int len;

    get_clip((void **) &data, &len);
    if (data) {
	char *p, *q;
	p = q = data;
	while (p < data+len) {
	    while (p < data+len &&
		   !(p <= data+len-sizeof(sel_nl) &&
		     !memcmp(p, sel_nl, sizeof(sel_nl))))
		p++;
	    back->send(s, q, p-q);
	    if (p <= data+len-sizeof(sel_nl) &&
		!memcmp(p, sel_nl, sizeof(sel_nl))) {
		back->send(s, "\015", 1);
		p += sizeof(sel_nl);
	    }
	    q = p;
	}
    }
    get_clip(NULL, NULL);
}

/*
 * Find out if there's a selection.
 */
int term_hasselection(Session *s) {

    return selstate == SELECTED;
}

static void deselect(Session *s) {
    selstate = NO_SELECTION;
    selstart = selend = scrtop;
}

void term_deselect(Session *s) {
    deselect(s);
    term_update(s);
}

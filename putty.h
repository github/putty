#ifndef PUTTY_PUTTY_H
#define PUTTY_PUTTY_H

#define PUTTY_REG_POS "Software\\SimonTatham\\PuTTY"

#ifdef macintosh
#define OPTIMISE_SCROLL
#endif

#ifdef macintosh
#include <MacTypes.h>
#include <Palettes.h>
#include <Controls.h>
#include <Windows.h>
typedef UInt32 DWORD;
#endif /* macintosh */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define ATTR_ACTCURS 0x80000000UL      /* active cursor (block) */
#define ATTR_PASCURS 0x40000000UL      /* passive cursor (box) */
#define ATTR_INVALID 0x20000000UL
#define ATTR_WRAPPED 0x10000000UL

#define ATTR_ASCII   0x00000000UL      /* normal ASCII charset ESC ( B */
#define ATTR_GBCHR   0x00100000UL      /* UK variant   charset ESC ( A */
#define ATTR_LINEDRW 0x00200000UL      /* line drawing charset ESC ( 0 */

#define ATTR_BOLD    0x00000100UL
#define ATTR_UNDER   0x00000200UL
#define ATTR_REVERSE 0x00000400UL
#define ATTR_FGMASK  0x0000F000UL
#define ATTR_BGMASK  0x000F0000UL
#define ATTR_FGSHIFT 12
#define ATTR_BGSHIFT 16

#define ATTR_DEFAULT 0x00098000UL
#define ATTR_DEFFG   0x00008000UL
#define ATTR_DEFBG   0x00090000UL
#define ATTR_CUR_XOR 0x000BA000UL
#define ERASE_CHAR   (ATTR_DEFAULT | ' ')
#define ATTR_MASK    0xFFFFFF00UL
#define CHAR_MASK    0x000000FFUL

#ifdef macintosh
#define SEL_NL { 13 }
#else
#define SEL_NL { 13, 10 }
#endif

/*
 * Global variables. Most modules declare these `extern', but
 * window.c will do `#define PUTTY_DO_GLOBALS' before including this
 * module, and so will get them properly defined.
 */
#ifdef PUTTY_DO_GLOBALS
#define GLOBAL
#else
#define GLOBAL extern
#endif

#define INBUF_SIZE 2048
#define INBUF_MASK (INBUF_SIZE-1)

#define OUTBUF_SIZE 2048
#define OUTBUF_MASK (OUTBUF_SIZE-1)

#define WM_NETEVENT  (WM_USER + 1)

typedef enum {
    TS_AYT, TS_BRK, TS_SYNCH, TS_EC, TS_EL, TS_GA, TS_NOP, TS_ABORT,
    TS_AO, TS_IP, TS_SUSP, TS_EOR, TS_EOF
} Telnet_Special;

typedef enum {
    MB_NOTHING, MB_SELECT, MB_EXTEND, MB_PASTE
} Mouse_Button;

typedef enum {
    MA_NOTHING, MA_CLICK, MA_2CLK, MA_3CLK, MA_DRAG, MA_RELEASE
} Mouse_Action;

typedef enum {
    VT_XWINDOWS, VT_OEMANSI, VT_OEMONLY, VT_POORMAN
} VT_Mode;

typedef struct Session Session;

typedef struct {
#ifdef macintosh
	char *(*init) (Session *, char *host, int port, char **realhost);
	int (*msg)(Session *);
#else /* not macintosh */
    char *(*init) (HWND hwnd, char *host, int port, char **realhost);
    int (*msg) (WPARAM wParam, LPARAM lParam);
#endif /* not macintosh */
    void (*send) (Session *, char *buf, int len);
    void (*size) (Session *);
    void (*special) (Session *, Telnet_Special code);
} Backend;

typedef struct {
    /* Basic options */
    char host[512];
    int port;
    enum { PROT_TELNET, PROT_SSH } protocol;
    int close_on_exit;
    /* SSH options */
    int nopty;
    /* Telnet options */
    char termtype[32];
    char termspeed[32];
    char environmt[1024];                    /* VAR\tvalue\0VAR\tvalue\0\0 */
    char username[32];
    int rfc_environ;
    /* Keyboard options */
    int bksp_is_delete;
    int rxvt_homeend;
    int linux_funkeys;
    int app_cursor;
    int app_keypad;
    int meta_modifiers;
    /* Terminal options */
    int savelines;
    int dec_om;
    int wrap_mode;
    int lfhascr;
    int win_name_always;
    int width, height;
    char font[64];
    int fontisbold;
    int fontheight;
    VT_Mode vtmode;
    /* Colour options */
    int try_palette;
    int bold_colour;
#ifdef macintosh
    PaletteHandle colours;
#else /* not macintosh */
    unsigned char colours[22][3];
#endif /* not macintosh */
    /* Selection options */
    int implicit_copy;
#ifdef macintosh
    int mouse_is_xterm;
#endif
    short wordness[256];
} Config;

typedef struct {
    /* Display buffers and pointers within them */
    unsigned long *text;	       /* buffer of text on terminal screen */
    unsigned long *scrtop;	       /* top of working screen */
    unsigned long *disptop;	       /* top of displayed screen */
    unsigned long *sbtop;	       /* top of scrollback */
    unsigned long *cpos;	       /* cursor position (convenience) */
    unsigned long *disptext;	       /* buffer of text on real screen */
    unsigned long *wanttext;	       /* buffer of text we want on screen */
    unsigned long *alttext;	       /* buffer of text on alt. screen */
    unsigned char *selspace;	       /* buffer for building selections in */

    /* Current state */
    unsigned long curr_attr;
    int curs_x, curs_y;	       /* cursor */
    int cset;		       /* 0 or 1: which char set is in GL */
    unsigned long cset_attr[2]; /* G0 and G1 char sets */

    /* Saved state */
    unsigned long  save_attr;
    int save_x, save_y;	       /* saved cursor position */
    int save_cset, save_csattr;     /* saved with cursor position */

    int marg_t, marg_b;	       /* scroll margins */

    /* Flags */
    int dec_om;		       /* DEC origin mode flag */
    int wrap, wrapnext;	       /* wrap flags */
    int insert;		       /* insert-mode flag */
    int rvideo;		       /* global reverse video flag */

    /*
     * Saved settings on the alternate screen.
     */
    int alt_x, alt_y, alt_om, alt_wrap, alt_wnext, alt_ins, alt_cset;
    int alt_t, alt_b;
    int alt_which;

    /* Escape sequence handler state */
#define ARGS_MAX 32		       /* max # of esc sequence arguments */
    int esc_args[ARGS_MAX];
    int esc_nargs;
    int esc_query;
#define OSC_STR_MAX 2048
    int osc_strlen;
    char osc_string[OSC_STR_MAX+1];
    int osc_w;

    unsigned char *tabs;
    int nl_count;

    enum {
	TOPLEVEL, IGNORE_NEXT,
	SEEN_ESC, SEEN_CSI, SET_GL, SET_GR,
	SEEN_OSC, SEEN_OSC_P, SEEN_OSC_W, OSC_STRING, OSC_MAYBE_ST,
	SEEN_ESCHASH
    } termstate;

    enum {
	NO_SELECTION, ABOUT_TO, DRAGGING, SELECTED
    } selstate;
    enum {
	SM_CHAR, SM_WORD, SM_LINE
    } selmode;
    unsigned long *selstart, *selend, *selanchor;
    short wordness[256];
} Term_State;

typedef struct Session {
    /* Config that created this session */
    Config cfg;
    /* Terminal emulator internal state */
    Term_State ts;
    /* Display state */
    int rows, cols, savelines;
    int font_width, font_height;
    int has_focus;
    /* Buffers */
    unsigned char inbuf[INBUF_SIZE];
    int inbuf_head, inbuf_reap;
    unsigned char outbuf[OUTBUF_SIZE];
    int outbuf_head, outbuf_reap;
    /* Emulator state */
    int app_cursor_keys, app_keypad_keys;
    /* Backend */
    Backend *back;
    /* Conveniences */
    unsigned long attr_mask;		/* Mask of attributes to display */
#ifdef macintosh
    short		fontnum;
    int			font_ascent;
    int			font_leading;
    int			font_boldadjust;
    WindowPtr		window;
    PaletteHandle	palette;
    ControlHandle	scrollbar;
    WCTabHandle		wctab;
#endif
} Session;


/*
 * Exports from display system
 */
extern void request_resize(Session *, int, int);
extern void do_text(Session *, int, int, char *, int, unsigned long);
extern void set_title(Session *, char *);
extern void set_icon(Session *, char *);
extern void set_sbar(Session *, int, int, int);
extern void pre_paint(Session *);
extern void post_paint(Session *);
extern void palette_set(Session *, int, int, int, int);
extern void palette_reset(Session *);
void write_clip (void *, int);
void get_clip (void **, int *);
extern void do_scroll(Session *, int, int, int);
void fatalbox (const char *, ...);
#ifdef macintosh
#pragma noreturn (fatalbox)
#endif
extern void beep (Session *s);
#define OPTIMISE_IS_SCROLL 1

/*
 * Exports from noise.c.
 */
void noise_get_heavy(void (*func) (void *, int));
void noise_get_light(void (*func) (void *, int));
void noise_ultralight(DWORD data);
void random_save_seed(void);

#ifndef macintosh
/*
 * Exports from windlg.c.
 */
int do_config (void);
int do_reconfig (HWND);
void do_defaults (char *);
void lognegot (char *);
void shownegot (HWND);
void showabout (HWND);
void verify_ssh_host_key(char *host, struct RSAKey *key);
#endif

/*
 * Exports from terminal.c.
 */

extern void term_init(Session *);
extern void term_size(Session *, int, int, int);
extern void term_out(Session *);
extern void term_paint(Session *, int, int, int, int);
extern void term_scroll(Session *, int, int);
extern void term_pwron(Session *);
extern void term_clrsb(Session *);
extern void term_mouse(Session *, Mouse_Button, Mouse_Action, int, int);
extern void term_copy(Session *);
extern void term_paste(Session *);
extern int term_hasselection(Session *);
extern void term_deselect (Session *);
extern void term_update (Session *);
extern void term_invalidate(Session *);

/*
 * Exports from telnet.c.
 */

extern Backend telnet_backend;

/*
 * Exports from ssh.c.
 */

extern Backend ssh_backend;

/*
 * Exports from sshrand.c.
 */

void random_add_noise(void *noise, int length);
void random_init(void);
int random_byte(void);
void random_get_savedata(void **data, int *len);

/*
 * Exports from misc.c.
 */

/* #define MALLOC_LOG  do this if you suspect putty of leaking memory */
#ifdef MALLOC_LOG
#define smalloc(z) (mlog(__FILE__,__LINE__), safemalloc(z))
#define srealloc(y,z) (mlog(__FILE__,__LINE__), saferealloc(y,z))
#define sfree(z) (mlog(__FILE__,__LINE__), safefree(z))
void mlog(char *, int);
#else
#define smalloc safemalloc
#define srealloc saferealloc
#define sfree safefree
#endif

void *safemalloc(size_t);
void *saferealloc(void *, size_t);
void safefree(void *);

/*
 * Exports from testback.c
 */

extern Backend null_backend;
extern Backend loop_backend;
extern Backend hexdump_backend;

/*
 * Exports from version.c.
 */
extern char ver[];

/*
 * A debug system.
 */
#ifdef DEBUG
#include <stdarg.h>
#define debug(x) (dprintf x)
static void dprintf(char *fmt, ...) {
    char buf[2048];
    DWORD dw;
    va_list ap;
    static int gotconsole = 0;

    if (!gotconsole) {
	AllocConsole();
	gotconsole = 1;
    }

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    WriteFile (GetStdHandle(STD_OUTPUT_HANDLE), buf, strlen(buf), &dw, NULL);
    va_end(ap);
}
#else
#define debug(x)
#endif

#endif

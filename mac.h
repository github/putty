/*
 * mac.h -- macintosh-specific declarations
 */

#ifndef _PUTTY_MAC_H
#define _PUTTY_MAC_H

#include <MacTypes.h>
#include <Events.h>
#include <Gestalt.h>
#include <MacWindows.h>

/* config.h or somesuch? */
#define WITH_OPENTRANSPORT

struct mac_gestalts {
    long qdvers;
    long apprvers;
    long cntlattr;
    long windattr;
};

extern struct mac_gestalts mac_gestalts;

#define HAVE_COLOR_QD() (mac_gestalts.qdvers > gestaltOriginalQD)

/* from macterm.c */
extern void mac_newsession(void);
extern void mac_activateterm(WindowPtr, Boolean);
extern void mac_adjusttermcursor(WindowPtr, Point, RgnHandle);
extern void mac_adjusttermmenus(WindowPtr);
extern void mac_updateterm(WindowPtr);
extern void mac_clickterm(WindowPtr, EventRecord *);
extern void mac_growterm(WindowPtr, EventRecord *);
extern void mac_keyterm(WindowPtr, EventRecord *);
extern void mac_menuterm(WindowPtr, short, short);
/* from maccfg.c */
extern void mac_loadconfig(Config *);
/* from macnet.c */
extern void macnet_eventcheck(void);
/* from opentpt.c */
extern Network_Stack otpt_stack;

#endif

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */

/* $Id: macnet.c,v 1.1.2.1 1999/04/01 21:26:03 ben Exp $ */
/*
 * Copyright (c) 1999 Ben Harris
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/*
 * macnet.c -- PuTTY-to-MacTCP glue
 */

#include <MacTypes.h>
#include <AddresXlation.h>
#incldue <MacTCP.h>
#include <MixedMode.h>
#include <Processes.h>

#include <stdlib.h>

#include "putty.h"

static short mtcp_refnum;
statis OSErr mtcp_initted = FALSE;

static void macnet_init(void);
static pascal void macnet_resolved(struct HostInfo *, char *);

#ifdef TARGET_RT_MAC_CFM
static RoutineDescriptor macnet_resolved_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppResultProcInfo, (ProcPtr)macnet_resolved);
#else
#define macnet_resolved_upp macnet_resolved
#endif

/*
 * Initialise networking.  Set mtcp_initted if it goes OK.
 */
static OSErr macnet_init(void) {
    OSErr err;

    err = OpenDriver(".IPP", &mtcp_refnum);
    if (err != noErr)
	return err;
    err = OpenResolver(NULL);
    if (err != noErr)
	return err;
    mtcp_initted = TRUE;

    /* XXX: otherwise report an error */
}

Socket *tcp_open(const char *host, int port, char **realhost) {
    ip_addr a;
    OSError err = noErr;
    Socket *s;

    s = smalloc(sizeof(struct Socket));
    if (!mtcp_initted)
	if ((err = macnet_init()) != noErr)
	    fatalbox("Couldn't init network (%d)", err);
    s->port = port;
    GetCurrentProcess(&s->psn);
    err = StrToAddr(host, &s->host_info, &macnet_resolved_upp, (char *)s);
    if (err != noErr)
	fatalbox("Host lookup failed (%d)", err);
    if (s->host_info.rtnCode != cacheFault)
	macnet_resolved(&s->host_info, s);
    return s;
}

static pascal void macnet_resolved(struct hostInfo *hi, char *cookie) {
    Socket *s = (Socket *)cookie;

    /* We should probably tell the process what's going on here. */
    /* Alternatively, we should kick off the next stage in the process */
    WakeUpProcess(&s->psn);
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */ 

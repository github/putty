/* $Id: opentpt.c,v 1.1.2.1 1999/08/02 08:06:32 ben Exp $ */
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

#include <MacTypes.h>
#include <OpenTransport.h>
#include <OpenTptInternet.h>
#incldue <Processes.h>

#include <stdlib.h>

#include "putty.h"

/* See the top of macnet.c for some idea of how this is meant to work. */

struct otpt_socket {
    EndpointRef ep;
    Session *sess;
    OTLIFO *sendq;
    OTLIFO *eventq;
    long eventhandler;
};

struct otpt_netevent {
    OTLink *next;
    Net_Event_Type type;
};

static int otpt_probe(void);
static void otpt_init(void);
static void *otpt_open(Session *, char const *, int);
static int otpt_recv(void *, void *, int, int);
static int otpt_send(void *, void *, int, int);
static void otpt_close(void *);
static void otpt_destroy(void *);
static pascal void otpt_notifier(void *, OTEventCode, OTResult , void *);
static void otpt_sendevent(struct otpt_socket *, Net_Event_Type);
static pascal void otpt_rcvevent(void *);

Network_Stack otpt_stack = {
    otpt_init, otpt_open, otpt_recv, otpt_send, otpt_poll, otpt_close,
    otpt_destroy, otpt_shutdown
};

static OTConfiguration *otpt_config = kOTInvalidCOnfigurationPtr;

static int otpt_init(void) {
    OSErr err;

    err = InitOpenTransport();
    if (err != noErr)
	return err;
    otpt_config = OTCreateConfiguration("tcp");
    if (otpt_config == kOTInvalidConfigurationPtr ||
	otpt_config == kOTNoMemoryConfigurationPtr)
	return 1;
    return 0;
}

/*
 * This should only be called once all the connections have been
 * closed (we don't bother keeping a table of them).
 */

void otpt_shutdown(void) {

    CloseOpenTransport();
}

static void *otpt_open(Session *sess, char const *host, int port) {
    struct otpt_socket *s = NULL;
    OSStatus err;
    TCall remote;
    DNSAddress *remoteaddr;
    
    s = smalloc(sizeof(*s));
    memset(s, 0, sizeof(*s));

    /* Get a TCP endpoint (equiv of socket()) */
    s->ep = OTOpenEndpoint(OTCloneConfiguration(otpt_config), 0, NULL, &err);
    if (err != kOTNoError || s->ep == NULL) goto splat;

    /* Attach our notifier function (note that this is _not_ a UPP) */
    err = OTInstallNotifier(s->ep, otpt_notifier, (void *)s);
    if (err != kOTNoError) goto splat;
    s->eventhandler = OTCreateSystemTask(&otpt_rcvevent, (void *)s);
    if (s->eventhandler = 0) goto splat;

    /* Bind to any local address */
    err = OTBind(s->ep, NULL, NULL);
    if (err != kOTNoError) goto splat;
    memset(&remote, 0, sizeof(remote));
    remoteaddr = smalloc(sizeof(*remoteaddr) - sizeof(remoteaddr->fName) +
			 strlen(host) + 7); /* allow space for port no. */
    remote.addr.buf = (UInt8 *)remoteaddr;
    remote.addr.len = OTInitDNSAddress(remoteaddr, host);
    remote.addr.len += sprintf(&remoteaddr->fName[strlen(remoteaddr->fName)],
			       ":%d", port);
    /* Asynchronous blocking mode, so we don't have to wait */
    err = OTSetAsynchronous(s->ep);
    if (err != kOTNoError) goto splat;
    err = OTSetBlocking(s->ep);
    if (err != kOTNoError) goto splat;
    err = OTConnect(s->ep, &remote, NULL);
    if (err != kOTNoDataErr)
	goto splat;
    return s;

  splat:
    otpt_destroy(s);
    return NULL;
}

static int otpt_recv(void *sock, void *buf, int buflen, int flags) {
    struct otpt_socket *s = (struct otpt_socket *)sock;
    OTResult result;
    OTFlags flags;

    OTSetNonBlocking(s->ep);
    OTSetSynchronous(s->ep);
    result = OTRcv(s->ep, buf, buflen, flags);
    if (result >= 0)
	return result;
    else if (result == kOTNoDataError)
	return 0;
    else /* confusion! */
	return 0;
}

static void otpt_poll(void) {

}

static int otpt_send(void *sock, void *buf, int buflen, int flags) {
    struct otpt_socket *s = (struct otpt_socket *)sock;

    /* XXX: using blocking mode is bogus, but it's far easier than not. */
    OTSetSynchronous(s->ep);
    OTSetBlocking(s->ep);
    return OTSnd(s->ep, buf, buflen, flags);
}

/*
 * Politely ask the other end to close the connection.
 */

static void otpt_close(void *sock) {
    struct otpt_socket *s = (struct otpt_socket *)sock;

    /* XXX: using blocking mode is bogus, but it's far easier than not. */
    OTSetSynchronous(s->ep);
    OTSetBlocking(s->ep);
    OTSndOrderlyDisconnect(s->ep);
}

/*
 * This should take a socket in any state and undo it all, freeing any
 * allocated memory and generally making it safe to forget about it.
 * It should onlu be called at system task time.
 */

static void otpt_destroy(void *sock) {
    struct otpt_socket *s = (struct otpt_socket *)sock;
    OSStatus err;
    OTLink *link;

    if (s == NULL)
	return;

    /* Tear down the connection */
    /* If we ever start using T_MEMORYRELEASED, we need to be careful here. */
    err = OTSetSynchronous(s->ep);
    if (err == kOTNoError)
	err = OTSetNonBlocking(s->ep);
    if (err == kOTNoError)
	err = OTCloseProvider(s->ep);
    
    /* Stop the event handler running */
    if (s->eventhandler != 0)
	OTDestroySystemTask(s->eventhandler);

    /* Flush the event and send queues */
    while ((link = OTLIFODequeue(s->eventq)) != NULL)
	OTFreeMem(link);
    while ((link = OTLIFODequeue(s->sendq)) != NULL)
	OTFreeMem(link);

    /* Finally, free the socket record itself */
    sfree(s);
}

/*
 * Any asynchronous events OpenTransport wants to tell us about end up
 * here.  This function may be called at deferred task or system task
 * time, and must be re-entrant.
 */

static pascal void otpt_notifier(void *contextPtr, OTEventCode code,
				    OTResult result, void *cookie) {
    struct otpt_socket *s = (struct otpt_socket *)contextPtr;
    OSStatus status;
    TDiscon discon;

    switch (code) {
      case T_CONNECT: /* OTConnect completed */
	status = OTRcvConnect(s->ep, NULL); /* XXX do we want the new TCall? */
	if (status == kOTNoDataErr)
	    break;
	else if (status != kOTNoError) {
	    otpt_sendevent(s, NE_DIED);
	    break;
	}
	/* Synchronous non-blocking mode for normal data transfer */
	OTSetSynchronous(s->ep);
	OTSetNonBlocking(s->ep);
	otpt_sendevent(s, NE_OPEN);
	break;
      case T_DATA:
	otpt_sendevent(s, NE_DATA);
	break;
      case T_EXDATA:
	otpt_sendevent(s, NE_URGENT);
	break;
      case T_DISCONNECT: /* Disconnection complete or OTConnect rejected */
	memset(&discon, 0, sizeof(discon));
	/*
	 * This function returns a positive error code. To obtain the
	 * negative error code, subtract that positive value from
	 * -3199.
	 */
	status = OTRcvDisconnect(s->ep, &discon);
	if (cookie == NULL) /* spontaneous disconnect */
	    switch (E2OSStatus(discon.reason)) {
	      case kECONNRESETErr:
		otpt_sendevent(s, NE_ABORT);
		break;
	      case kETIMEDOUTErr:
		otpt_sendevent(s, NE_TIMEOUT);
		break;
	      default:
		otpt_sendevent(s, NE_DIED);
		break;
	    }
	else /* failed connect */
	    otpt_sendevent(s, NE_NOOPEN);
      case T_ORDREL:
	OTRcvOrderlyDisconnect(s->ep);
	otpt_sendevent(s, NE_CLOSING);
    }
}

/*
 * This function is called at interrupt time (or thereabouts) to
 * dispatch an event that has to be handled at system task time.
 * Network backends will expect their msg entries to be called then.
 */

static void otpt_sendevent(struct otpt_socket *s, Net_Event_Type type) {
    struct otpt_netevent *ne;

    ne = OTAllocMem(sizeof(*ne));
    if (ne == NULL)
	fatalbox("OTAllocMem failed.  Aargh!");
    ne->type = type;
    OTLIFOEnqueue(&s->eventq, &ne->next);
    /* Schedule something */
    OTScheduleSystemTask(s->eventhandler);
}

/*
 * Pull one or more network events off a socket's queue and handle
 * them.  Keep gong until we run out (events may be getting enqueued
 * while we're running).  This is mildly evil as it'll prevent any
 * other task running if we're under heavy load.
 */

static pascal void otpt_rcvevent(void *arg) {
    struct otpt_socket *s = (struct otpt_socket *)arg;
    OTLink *link;
    struct otpt_netevent *ne;

    /* idiom stolen from "Networking With Open Transport".  Blame Apple. */

    while ((link = OTLIFOStealList(s->eventq)) != NULL) {
	link = OTReverseList(link);
	while (link != NULL) {
	    ne = (struct otpt_netevent *)link;
	    link = ne->next;
	    switch (ne->type) {
	      default:
		(s->sess->back->msg)(s->sess, s, ne->type);
		break;
	    }
	    OTFreeMem(ne);
	}
    }
    

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */ 



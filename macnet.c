/* $Id: macnet.c,v 1.1.2.3 1999/04/04 18:23:34 ben Exp $ */
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
#include <AddressXlation.h>
#include <Devices.h>
#include <MacTCP.h>
#include <MixedMode.h>
#include <OSUtils.h>
#include <Processes.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "putty.h"

/*
 * The theory behind this stuff:
 *
 * net_recv attempts to deliver any incoming data waiting on the
 * queue.  Since MacTCP maintains a buffer for incoming data, there's
 * no need for us to run asynchronous TCPRcvs, and we just do a
 * synchronous one if we detect some data waiting.  Since TCPRcv can't
 * be given a timeout of zero, we use TCPStatus to work out if there's
 * anything waiting first.
 *
 * Sending data is trickier.  TCPSend reserves the right to block
 * until everything we've sent is ACKed, which means we have to use it
 * asynchronously.  In order to make life easier for backends, and to
 * save a proliferation of circular buffers, we guarantee to take data
 * off the hands of the backend as soon as it gives it to us.  This is
 * reasonable because currently there's no way for the backend to say
 * it can't take data, and once it's got them, it may as well give
 * them to us.
 * 
 * Anyway, in order to avoid a fixed-size buffer overflowing, the send
 * buffer is kept as a queue of blocks.  When net_send is called, we
 * malloc a new block and stick it on the queue.  If the queue was
 * empty, we kick off a new asynchronous TCPSend to handle our block.
 *
 */

typedef struct Socket {
    TCPiopb iopb; /* current MacTCP operation */
    TCPiopb spareiopb; /* for closing etc */
    hostInfo hostinfo;
    int port;
    ProcessSerialNumber psn;
    Session *s;
    long a5;
    QHdr sendq; /* Blocks waiting to be sent */
} Socket;

typedef struct {
    QElem qelem;
    int flags;
    wdsEntry wds;
    short wdsterm;
} Send_Buffer;

/*
 * Yes, I know the struct QElem has a short[1] to represent the user
 * data.  I'm ignoring it because it makes my code prettier and
 * improves the alignment.
 */

typedef struct {
    QElem qelem;
    Socket *sock;
    Net_Event_Type type;
} NetEvent;

#define TCPBUF_SIZE 8192

static QHdr macnet_eventq;
static QHdr macnet_freeq;

static short mtcp_refnum;
static int mtcp_initted = FALSE;

static OSErr macnet_init(void);
static pascal void macnet_resolved(hostInfo *, char *);
static void macnet_completed_open(TCPiopb*);
static void macnet_completed_send(TCPiopb*);
static void macnet_sent(Socket *);
static void macnet_startsend(Socket *);
static void macnet_completed_close(TCPiopb*);
static pascal void macnet_asr(StreamPtr, unsigned short, Ptr, unsigned short,
			      ICMPReport *);
static void macnet_sendevent(Socket *, Net_Event_Type);

#if TARGET_RT_MAC_CFM
static RoutineDescriptor macnet_resolved_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppResultProcInfo, (ProcPtr)macnet_resolved);
static RoutineDescriptor macnet_completed_open_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo,
			     (ProcPtr)macnet_completed_open);
static RoutineDescriptor macnet_complete_send_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo,
			     (ProcPtr)macnet_completed_send);
static RoutineDescriptor macnet_completed_close_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo,
			     (ProcPtr)macnet_completed_close);
static RoutineDescriptor macnet_asr_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppTCPNotifyProcInfo, (ProcPtr)macnet_asr);
#else
#define macnet_resolved_upp macnet_resolved
#define macnet_completed_open_upp macnet_completed_open
#define macnet_completed_send_upp macnet_completed_send
#define macnet_completed_close_upp macnet_completed_close
#define macnet_asr_upp macnet_asr
#endif

/*
 * Number of outstanding network events allowed. 
 */
#define NUM_EVENTS 16

/*
 * Initialise networking.  Set mtcp_initted if it goes OK.
 */
static OSErr macnet_init(void) {
    OSErr err;
    NetEvent *eventblock;
    int i;

    /*
     * FIXME: This is hideously broken, in that we're meant to faff
     * with unit numbers and stuff, and we blatantly don't.
     */
    err = opendriver(".IPP", &mtcp_refnum);
    if (err != noErr)
	return err;
    err = OpenResolver(NULL);
    if (err != noErr)
	return err;
    /* Set up the event queues, and fill the free queue with events 
     */
    macnet_eventq.qFlags = 0;
    macnet_eventq.qHead = macnet_eventq.qTail = NULL;
    macnet_freeq.qFlags = 0;
    macnet_freeq.qHead = macnet_eventq.qTail = NULL;
    eventblock = smalloc(NUM_EVENTS * sizeof(NetEvent));
    for (i = 0; i < NUM_EVENTS; i++)
	Enqueue(&eventblock[i].qelem, &macnet_freeq);
    mtcp_initted = TRUE;
    return 0;
}

Socket *net_open(Session *s, char *host, int port) {
    ip_addr a;
    OSErr err = noErr;
    Socket *sock;
    void *tcpbuf;

    /*
     * First, get hold of all the memory we'll need (a lot of the
     * later stuff happens at interrupt time)
     */
    sock = smalloc(sizeof(struct Socket));
    memset(sock, 0, sizeof(*sock));
    tcpbuf = smalloc(TCPBUF_SIZE);

    /* Make a note of anything we don't want to forget */
    sock->port = port;
    GetCurrentProcess(&sock->psn);
    sock->a5 = SetCurrentA5();

    /* Get MacTCP running if it's not already */
    if (!mtcp_initted)
	if ((err = macnet_init()) != noErr)
	    fatalbox("Couldn't init network (%d)", err);

    /* Get ourselves a TCP stream to play with */
    sock->iopb.ioCRefNum = mtcp_refnum;
    sock->iopb.csCode = TCPCreate;
    sock->iopb.csParam.create.rcvBuff = tcpbuf;
    sock->iopb.csParam.create.rcvBuffLen = TCPBUF_SIZE;
    sock->iopb.csParam.create.notifyProc = macnet_asr_upp;
    sock->iopb.csParam.create.userDataPtr = (Ptr)sock;
    /* This could be done asynchronously, but I doubt it'll take long. */
    err = PBControlSync((ParmBlkPtr)&sock->iopb);
    if (err != noErr)
	fatalbox("TCP stream open failed (%d)", err);

    err = StrToAddr(host, &sock->hostinfo, &macnet_resolved_upp, (char *)sock);
    if (err != noErr)
	fatalbox("Host lookup failed (%d)", err);
    if (sock->hostinfo.rtnCode != cacheFault)
	macnet_resolved(&sock->hostinfo, (char *)sock);
    return sock;
}

static pascal void macnet_resolved(hostInfo *hi, char *cookie) {
    Socket *sock = (Socket *)cookie;
    OSErr err;
#if !TARGET_RT_CFM
    long olda5;

    olda5 = SetA5(sock->a5);
#endif
    /*
     * We've resolved a name, so now we'd like to connect to it (or
     * report an error).
     */
    switch (sock->hostinfo.rtnCode) {
      case noErr:
	/* Open a connection */
	sock->iopb.ioCompletion = macnet_completed_open_upp;
	sock->iopb.csCode = TCPActiveOpen;
	sock->iopb.csParam.open.validityFlags = typeOfService;
	sock->iopb.csParam.open.commandTimeoutValue = 0; /* unused */
	sock->iopb.csParam.open.remoteHost = sock->hostinfo.addr[0]; /*XXX*/
	sock->iopb.csParam.open.remotePort = sock->port;
	/* localHost is set by MacTCP. */
	sock->iopb.csParam.open.localPort = 0;
	sock->iopb.csParam.open.tosFlags = lowDelay;
	sock->iopb.csParam.open.dontFrag = 0;
	sock->iopb.csParam.open.timeToLive = 0; /* default */
	sock->iopb.csParam.open.security = 0;
	sock->iopb.csParam.open.optionCnt = 0;
	sock->iopb.csParam.open.userDataPtr = (char *)sock;
	err = PBControlSync((ParmBlkPtr)&sock->iopb);
	if (err != noErr)
	    macnet_sendevent(sock, NE_NOOPEN);
	break;
      default: /* Something went wrong */
	macnet_sendevent(sock, NE_NOHOST);
	break;
    }
#if !TARGET_RT_CFM
    SetA5(olda5);
#endif
}

static void macnet_completed_open(TCPiopb *iopb) {
    Socket *sock = (Socket *)iopb->csParam.open.userDataPtr;
#if !TARGET_RT_CFM
    long olda5;

    olda5 = SetA5(sock->a5);
#endif
    switch (iopb->ioResult) {
      case noErr:
	macnet_sendevent(sock, NE_OPEN);
	break;
      default:
	macnet_sendevent(sock, NE_NOOPEN);
	break;
    }
#if !TARGET_RT_CFM
    SetA5(olda5);
#endif
}

static pascal void macnet_asr(StreamPtr tcpstream, unsigned short eventcode,
			      Ptr cookie, unsigned short terminreason,
			      ICMPReport *icmpmsg) {
    Socket *sock = (Socket *)cookie;
#if !TARGET_RT_CFM
    long olda5;

    olda5 = SetA5(sock->a5);
#endif
    switch (eventcode) {
      case TCPClosing:
	macnet_sendevent(sock, NE_CLOSING);
	break;
      case TCPULPTimeout:
	macnet_sendevent(sock, NE_TIMEOUT);
	break;
      case TCPTerminate:
	switch (terminreason) {
	  case TCPRemoteAbort:
	    macnet_sendevent(sock, NE_ABORT);
	    break;
	  default:
	    macnet_sendevent(sock, NE_DIED);
	    break;
	}
	break;
      case TCPDataArrival:
	macnet_sendevent(sock, NE_DATA);
	break;
      case TCPUrgent:
	macnet_sendevent(sock, NE_URGENT);
	break;
      case TCPICMPReceived:
	switch (icmpmsg->reportType) {
	  case portUnreach:
	    macnet_sendevent(sock, NE_REFUSED);
	    break;
	}
	break;
    }
#if !TARGET_RT_CFM
    SetA5(olda5);
#endif
}

/*
 * Send a block of data.
 */

int net_send(Socket *sock, void *buf, int buflen, int flags) {
    OSErr err;
    Send_Buffer *buff;

    buff = smalloc(sizeof(Send_Buffer) + buflen);
    buff->flags = flags;
    buff->wds.length = buflen;
    buff->wds.ptr = (Ptr)&buff[1]; /* after the end of the struct */
    buff->wdsterm = 0;
    memcpy(&buff[1], buf, buflen);
    Enqueue(&buff->qelem, &sock->sendq);
    /* Kick off the transmit if the queue was empty */
    if (sock->sendq.qHead == &buff->qelem)
	macnet_startsend(sock);
}

/*
 * This is called once every time round the event loop to check for
 * network events and handle them.
 */
void macnet_eventcheck() {
    NetEvent *ne;

    if (!mtcp_initted)
	return;
    ne = (NetEvent *)macnet_eventq.qHead;
    if (ne == NULL)
	return;
    Dequeue(&ne->qelem, &macnet_eventq);
    switch (ne->type) {
      case NE_SENT:
	macnet_sent(ne->sock);
	break;
      default:
	(ne->sock->s->back->msg)(ne->sock->s, ne->sock, ne->type);
	break;
    }
    Enqueue(&ne->qelem, &macnet_freeq);
}

/*
 * The block at the head of the send queue has finished sending, so we
 * can free it.  Kick off the next transmission if there is one.
 */
static void macnet_sent(Socket *sock) {
    Send_Buffer *buff;

    assert(sock->sendq.qHead != NULL);
    buff = (Send_Buffer *)sock->sendq.qHead;
    Dequeue(&buff->qelem, &sock->sendq);
    sfree(buff);
    if (sock->sendq.qHead != NULL)
	macnet_startsend(sock);
}

/*
 * There's a block on the head of the send queue which needs to be
 * sent.
 */

static void macnet_startsend(Socket *sock) {
    Send_Buffer *buff;
    OSErr err;

    buff = (Send_Buffer *)sock->sendq.qHead;
    sock->iopb.csCode = TCPSend;
    sock->iopb.csParam.send.validityFlags = 0;
    sock->iopb.csParam.send.pushFlag = buff->flags & SEND_PUSH ? true : false;
    sock->iopb.csParam.send.urgentFlag = buff->flags & SEND_URG ? true : false;
    sock->iopb.csParam.send.wdsPtr = (Ptr)&buff->wds;
    sock->iopb.csParam.send.userDataPtr = (char *)sock;
    err = PBControlAsync((ParmBlkPtr)&sock->iopb);
}

int net_recv(Socket *sock, void *buf, int buflen, int flags) {
    TCPiopb iopb;
    OSErr err;
    int avail, want, got;

    memcpy(&iopb, &sock->iopb, sizeof(TCPiopb));
    /* Work out if there's anything to recieve (we don't want to block) 
 */
    iopb.csCode = TCPStatus;
    err = PBControlSync((ParmBlkPtr)&iopb);
    if (err != noErr)
	return 0; /* macnet_asr should catch it anyway */
    avail = iopb.csParam.status.amtUnreadData;
    if (avail == 0)
	return 0;
    want = avail < buflen ? avail : buflen;
    iopb.csCode = TCPRcv;
    iopb.csParam.receive.rcvBuff = buf;
    iopb.csParam.receive.rcvBuffLen = want;
    err = PBControlSync((ParmBlkPtr)&iopb);
    if (err != noErr)
	return 0;
    return iopb.csParam.receive.rcvBuffLen;
}
	

void net_close(Socket *sock) {
    OSErr err;

    /*
     * This might get called in the middle of processing another
     * request on the socket, so we have a spare parameter block for
     * this purpose (allocating one dynamically would mean having to
     * free it, which we can't do at interrupt time).
     */
    memcpy(&sock->spareiopb, &sock->iopb, sizeof(TCPiopb));
    sock->spareiopb.ioCompletion = macnet_completed_close_upp;
    sock->spareiopb.csCode = TCPClose;
    sock->spareiopb.csParam.close.validityFlags = 0;
    sock->spareiopb.csParam.close.userDataPtr = (char *)sock;
    err = PBControlAsync((ParmBlkPtr)&sock->spareiopb);
    switch (err) {
      case noErr:
      case connectionClosing:
      case connectionTerminated: /* We'll get an ASR */
	break;
      default:
	macnet_sendevent(sock, NE_DIED);
	break;
    }
}

static void macnet_completed_close(TCPiopb* iopb) {
    Socket *sock = (Socket *)iopb->csParam.close.userDataPtr;
#if !TARGET_RT_CFM
    long olda5;

    olda5 = SetA5(sock->a5);
#endif
    switch (iopb->ioResult) {
      case noErr:
	macnet_sendevent(sock, NE_CLOSED);
	break;
      case connectionClosing:
      case connectionTerminated:
	break;
      default:
	macnet_sendevent(sock, NE_DIED);
	break;
    }
#if !TARGET_RT_CFM
    SetA5(olda5);
#endif
}

/*
 * Free all the data structures associated with a socket and tear down
 * any connection through it.
 */
void net_destroy(Socket *sock) {
    TCPiopb iopb;
    OSErr err;

    /*
     * Yes, we need _another_ iopb, as there may be a send _and_ a
     * close outstanding.  Luckily, destroying a socket is
     * synchronous, so we can allocate this one dynamically.
     */
    memcpy(&iopb, &sock->iopb, sizeof(TCPiopb));
    iopb.csCode = TCPRelease;
    err = PBControlSync((ParmBlkPtr)&iopb);
    sfree(iopb.csParam.create.rcvBuff);
    sfree(sock);
}

static void macnet_sendevent(Socket *sock, Net_Event_Type type) {
    NetEvent *ne;

    ne = (NetEvent *)macnet_freeq.qHead;
    assert (ne != NULL);
    Dequeue(&ne->qelem, &macnet_freeq);
    ne->sock = sock;
    ne->type = type;
    Enqueue(&ne->qelem, &macnet_eventq);
    WakeUpProcess(&sock->psn);
}

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */ 

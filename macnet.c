/* $Id: macnet.c,v 1.1.2.2 1999/04/03 21:53:29 ben Exp $ */
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
//    unsigned char *inbuf;
//    int inbuf_head, inbuf_reap, inbuf_size;
//    unsigned char *outbuf;
//    int outbuf_head, outbuf_reap, outbuf_size;
    ProcessSerialNumber psn;
    Session *s;
    UInt32 a5;
    qHdr sendq; /* Blocks waiting to be sent */
    qHdr freeq; /* Blocks sent, waiting to be freed */
} Socket;

typedef struct {
    QElem qelem;
    int flags;
    int len;
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
static void macnet_opened(TCPiopb*);
static void macnet_sent(TCPiopb*);
static void macnet_closed(TCPiopb*);
static pascal void macnet_asr(StreamPtr, unsigned short, Ptr, unsigned short,
			      ICMPReport *);
static void macnet_sendevent(Socket *, Net_Event_Type);

#if TARGET_RT_MAC_CFM
static RoutineDescriptor macnet_resolved_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppResultProcInfo, (ProcPtr)macnet_resolved);
static RoutineDescriptor macnet_opened_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo,
			     (ProcPtr)macnet_opened);
static RoutineDescriptor macnet_sent_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo, (ProcPtr)macnet_sent);
static RoutineDescriptor macnet_closed_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo,
			     (ProcPtr)macnet_closed);
static RoutineDescriptor macnet_asr_upp =
    BUILD_ROUTINE_DESCRIPTOR(uppTCPNotifyProcInfo, (ProcPtr)macnet_asr);
#else
#define macnet_resolved_upp macnet_resolved
#define macnet_opened_upp macnet_opened
#define macnet_sent_upp macnet_sent
#define macnet_closed_upp macnet_closed
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

    err = opendriver(".IPP", &mtcp_refnum);
    if (err != noErr)
	return err;
    err = OpenResolver(NULL);
    if (err != noErr)
	return err;
    /* Set up the event queues, and fill the free queue with events */
    macnet_eventq.qFlags = 0;
    macnet_eventq.qHead = macnet_eventq.qTail = NULL;
    macnet_freeq.qFlags = 0;
    macnet_freeq.qHead = macnet_eventq.qTail = NULL;
    eventblock = smalloc(NUM_EVENTS * sizeof(NetEvent));
    for (i = 0; i < NUM_EVENTS; i++)
	Enqueue(&eventblock[i].qelem, &macnet_freeq);
    mtcp_initted = TRUE;
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
    UInt32 olda5;

    olda5 = SetA5(sock->a5);
    /*
     * We've resolved a name, so now we'd like to connect to it (or
     * report an error).
     */
    switch (sock->hostinfo.rtnCode) {
      case noErr:
	/* Open a connection */
	sock->iopb.ioCompletion = macnet_opened_upp;
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
    SetA5(olda5);
}

static void macnet_opened(TCPiopb *iopb) {
    Socket *sock = (Socket *)iopb->csParam.open.userDataPtr;
    UInt32 olda5;

    olda5 = SetA5(sock->a5);
    switch (iopb->ioResult) {
      case noErr:
	macnet_sendevent(sock, NE_OPEN);
	break;
      default:
	macnet_sendevent(sock, NE_NOOPEN);
	break;
    }
    SetA5(olda5);
}

static pascal void macnet_asr(StreamPtr tcpstream, unsigned short eventcode,
			      Ptr cookie, unsigned short terminreason,
			      ICMPReport *icmpmsg) {
    Socket *sock = (Socket *)cookie;
    UInt32 olda5;

    olda5 = SetA5(sock->a5);
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
    SetA5(olda5);
}

/*
 * Send a block of data.
 */

int net_send(Socket *sock, void *buf, int buflen, int flags) {{
    OSErr err;
    Send_Buffer *buff;

    buff = smalloc(sizeof(Send_Buffer) + buflen);
    buff->flags = flags;
    buff->len = buflen;
    memcpy(buff + 1, buf, buflen);
    Enqueue(&buff->qelem, &sock->sendq);
    macnet_start(sock);
}

int net_recv(Socket *sock, void *buf, int buflen, int flags) {
    TCPiopb iopb;
    OSErr err;
    int avail, want, got;

    memcpy(&iopb, &sock->iopb, sizeof(TCPiopb));
    /* Work out if there's anything to recieve (we don't want to block) */
    iopb.csCode = TCPStatus;
    err = PBControlSync((ParmBlkPtr)&iopb);
    if (err != noErr)
	return 0; /* macnet_asr should catch it anyway */
    avail = iopb.csParam.status.amtUnreadData;
    if (avail == 0)
	return 0;
    want = avail < buflen ? avail : buflen;
    iopb.csCode = TCPRcv;
    iopb.csParam.receive.buffPtr = buf;
    iopb.csParam.receive.buffLen = want;
    err = PBControlSync((ParmBlkPtr)&iopb);
    if (err != noErr)
	return 0;
    return iopb.csParam.receive.buffLen;
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
    sock->spareiopb.ioCompletion = macnet_closed_upp;
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

static void macnet_closed(TCPiopb* iopb) {
    Socket *sock = (Socket *)iopb->csParam.close.userDataPtr;
    UInt32 olda5;

    olda5 = SetA5(sock->a5);
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
    SetA5(olda5);
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

/*
 * macnet.h -- Mac OS networtking stuff for PuTTY
 */

#ifndef _PUTTY_MACNET_H
#define _PUTTY_MACNET_H

#include <MacTypes.h>
#include <AddressXlation.h>
#include <MacTCP.h>
#include <Processes.h>

typedef struct {
    StreamPtr tcp_stream;
    struct HostInfo host_info;
    int port;
    unsigned char *inbuf;
    int inbuf_head, inbuf_reap, inbuf_size;
    unsigned char *outbuf;
    int outbuf_head, outbuf_reap, outbuf_size;
    ProcessSerialNumber psn;
} Socket;

typedef Socket *SOCKET

#define INVALID_SOCKET NULL

#define MSG_OOB 1

extern int send(SOCKET, const void *, size_t, int);
extern int recv(SOCKET, void *, size_t, int);
extern SOCKET tcp_open(const char *, int, char **);
extern void tcp_close(SOCKET);
extern void tcp_abort(SOCKET);

#endif

/*
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */

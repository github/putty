#include <stdio.h>
#include <stdlib.h>
#ifndef macintosh
#include <winsock.h>
#endif

#include "putty.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#include "ssh.h"

/* Coroutine mechanics for the sillier bits of the code */
#define crBegin1(l)	(l) = 0;
#define crBegin2(l)	switch(l) { case 0:;
#define crBegin(l)		crBegin1(l); crBegin2(l);
#define crFinish(l,z)	} (l) = 0; return (l)
#define foolemacs {
#define crFinishV(l)	} (l) = 0; return
#define crReturn(l,z)	\
	do {\
	    (l)=__LINE__; return (z); case __LINE__:;\
	} while (0)
#define crReturnV(l)	\
	do {\
	    (l)=__LINE__; return; case __LINE__:;\
	} while (0)
#define crStop(l,z)	do{ (l) = 0; return (z); }while(0)
#define crStopV(l)		do{ (l) = 0; return; }while(0)

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

struct Packet {
    long length;
    int type;
    unsigned long crc;
    unsigned char *data;
    unsigned char *body;
    long maxlen;
};

struct ssh_private {
    SOCKET s;
    unsigned char session_key[32];
    struct ssh_cipher *cipher;
    char *savedhost;
    enum {
        SSH_STATE_BEFORE_SIZE,
	SSH_STATE_INTERMED,
        SSH_STATE_SESSION,
        SSH_STATE_CLOSED
    } ssh_state;
    int size_needed;
#ifdef FWHACK
    char *FWhost;
    int FWport;
#endif
    struct Packet pktin;
    struct Packet pktout;
    /* State for ssh_gotdata coroutine */
    int gd_line;
    long len, biglen, to_read;
    unsigned char *p;
    int i, pad;
    int chunk;
    char *sversion;
    size_t sversion_space, sversion_len;
    /* State for ssh_protocol coroutine */
    int pr_line;
};

static void s_write (Session *sess, unsigned char *buf, int len) {
    struct ssh_private *sp = (struct ssh_private*)sess->back_priv;

    while (len > 0) {
	int i = net_send (sp->s, buf, len, 0);
	if (i > 0)
	    len -= i, buf += i;
    }
}

static int s_read (Session *sess, unsigned char *buf, int len) {
    struct ssh_private *sp = (struct ssh_private*)sess->back_priv;

    int ret = 0;
    while (len > 0) {
	int i = net_recv (sp->s, buf, len, 0);
	if (i > 0)
	    len -= i, buf += i, ret += i;
	else
	    return i;
    }
    return ret;
}

static void c_write (Session *sess, char *buf, int len) {

    while (len--) {
	int new_head = (sess->inbuf_head + 1) & INBUF_MASK;
	if (new_head != sess->inbuf_reap) {
	    sess->inbuf[sess->inbuf_head] = *buf++;
	    sess->inbuf_head = new_head;
	}
    }
}

static void ssh_protocol(Session *,unsigned char *in, int inlen, int ispkt);
static void ssh_size(void);

static void ssh_gotdata(Session *sess, unsigned char *data, int datalen) {
    struct ssh_private *sp = (struct ssh_private *)sess->back_priv;
    unsigned char *eol;
    size_t fraglen = datalen;
    static char vstring[] = "SSH-1.5-PuTTY\n";

    crBegin(sp->gd_line);

#ifdef FWHACK
    /* Should wait for "SSH-" */
#endif
    sp->sversion = smalloc(16); /* enough for "SSH-1.5-1.2.27\n" */
    sp->sversion_space = 16;
    sp->sversion_len = 0;
    do {

	while (datalen == 0)
	    crReturnV(sp->gd_line);
	eol = memchr(data, '\n', datalen);
	if (eol != NULL)
	    fraglen = eol + 1 - data; /* include \n */
	if (sp->sversion_len + fraglen > sp->sversion_space) {
	    /* FIXME Sanity-check length */
	    srealloc(sp->sversion, sp->sversion_len + fraglen);
	    sp->sversion_space = sp->sversion_len + fraglen;
	}
	memcpy(sp->sversion + sp->sversion_len, data, fraglen);
	data += fraglen; datalen -= fraglen;
    } while (eol == NULL);
    sp->sversion[sp->sversion_len - 1] = '\0';

    if (sp->sversion_len < 4 || memcmp(sp->sversion, "SSH-", 4) != 0)
	/* XXX explode! */;

    /*
     * We ignore the rest of the banner on the grounds that we only
     * speak SSH 1.5 anyway.  If the server can't, that's its problem.
     */

    s_write(sess, (unsigned char *)vstring, strlen(vstring));

    /* End of do_ssh_init */

    while (1) {
	for (sp->i = sp->len = 0; sp->i < 4; sp->i++) {
	    while (datalen == 0)
		crReturnV(sp->gd_line);
	    sp->len = (sp->len << 8) + *data;
	    data++, datalen--;
	}

#ifdef FWHACK
        if (sp->len == 0x52656d6f) {       /* "Remo"te server has closed ... */
            sp->len = 0x300;               /* big enough to carry to end */
        }
#endif

	sp->pad = 8 - (sp->len%8);

	sp->biglen = sp->len + sp->pad;

	sp->len -= 5;		       /* type and CRC */

	sp->pktin.length = sp->len;
	if (sp->pktin.maxlen < sp->biglen) {
	    sp->pktin.maxlen = sp->biglen;
	    sp->pktin.data = (sp->pktin.data == NULL ? smalloc(sp->biglen) :
			srealloc(sp->pktin.data, sp->biglen));
	}

	sp->p = sp->pktin.data, sp->to_read = sp->biglen;
	while (sp->to_read > 0) {
	    sp->chunk = sp->to_read;
	    while (datalen == 0)
		crReturnV(sp->gd_line);
	    if (sp->chunk > datalen)
		sp->chunk = datalen;
	    memcpy(sp->p, data, sp->chunk);
	    data += sp->chunk;
	    datalen -= sp->chunk;
	    sp->p += sp->chunk;
	    sp->to_read -= sp->chunk;
	}

	if (sp->cipher)
	    sp->cipher->decrypt(sp->pktin.data, sp->biglen);

	sp->pktin.type = sp->pktin.data[sp->pad];
	sp->pktin.body = sp->pktin.data+sp->pad+1;

	if (sp->pktin.type == 36) {	       /* SSH_MSG_DEBUG */
	    /* FIXME: log it */
	} else
	    ssh_protocol(sess, NULL, 0, 1);
    }
    crFinishV(sp->gd_line);
}

static void s_wrpkt_start(Session *sess, int type, int len) {
    struct ssh_private *sp = (struct ssh_private *)sess->back_priv;
    int pad, biglen;

    len += 5;			       /* type and CRC */
    pad = 8 - (len%8);
    biglen = len + pad;

    sp->pktout.length = len-5;
    if (sp->pktout.maxlen < biglen) {
	sp->pktout.maxlen = biglen;
	sp->pktout.data = (sp->pktout.data == NULL ? smalloc(biglen+4) :
		       srealloc(sp->pktout.data, biglen+4));
    }

    sp->pktout.type = type;
    sp->pktout.body = sp->pktout.data+4+pad+1;
}

static void s_wrpkt(Session *sess) {
    struct ssh_private *sp = (struct ssh_private *)sess->back_priv;

    int pad, len, biglen, i;
    unsigned long crc;

    len = sp->pktout.length + 5;	       /* type and CRC */
    pad = 8 - (len%8);
    biglen = len + pad;

    sp->pktout.body[-1] = sp->pktout.type;
    for (i=0; i<pad; i++)
	sp->pktout.data[i+4] = random_byte();
    crc = crc32(sp->pktout.data+4, biglen-4);

    sp->pktout.data[biglen+0] = (unsigned char) ((crc >> 24) & 0xFF);
    sp->pktout.data[biglen+1] = (unsigned char) ((crc >> 16) & 0xFF);
    sp->pktout.data[biglen+2] = (unsigned char) ((crc >> 8) & 0xFF);
    sp->pktout.data[biglen+3] = (unsigned char) (crc & 0xFF);

    sp->pktout.data[0] = (len >> 24) & 0xFF;
    sp->pktout.data[1] = (len >> 16) & 0xFF;
    sp->pktout.data[2] = (len >> 8) & 0xFF;
    sp->pktout.data[3] = len & 0xFF;

    if (sp->cipher)
	sp->cipher->encrypt(sp->pktout.data+4, biglen);

    s_write(sess, sp->pktout.data, biglen+4);
}

static int do_ssh_init(Session *sess) {
}

static void ssh_protocol(Session *sess, unsigned char *in, int inlen,
                         int ispkt) {
    struct ssh_private *sp = (struct ssh_private *)sess->back_priv;
    int i, j, len;
    unsigned char session_id[16];
    unsigned char *rsabuf, *keystr1, *keystr2;
    unsigned char cookie[8];
    struct RSAKey servkey, hostkey;
    struct MD5Context md5c;
    unsigned long supported_ciphers_mask;
    int cipher_type;

    extern struct ssh_cipher ssh_3des;
    extern struct ssh_cipher ssh_blowfish;

    crBegin(sp->pr_line);

    random_init();

    while (!ispkt)
	crReturnV(sp->pr_line);

    if (sp->pktin.type != 2)
	fatalbox("Public key packet not received");

    memcpy(cookie, sp->pktin.body, 8);

    MD5Init(&md5c);

    i = makekey(sp->pktin.body+8, &servkey, &keystr1);

    j = makekey(sp->pktin.body+8+i, &hostkey, &keystr2);

    supported_ciphers_mask = (sp->pktin.body[12+i+j] << 24) |
                             (sp->pktin.body[13+i+j] << 16) |
                             (sp->pktin.body[14+i+j] << 8) |
                             (sp->pktin.body[15+i+j]);

    MD5Update(&md5c, keystr2, hostkey.bytes);
    MD5Update(&md5c, keystr1, servkey.bytes);
    MD5Update(&md5c, sp->pktin.body, 8);

    MD5Final(session_id, &md5c);

    for (i=0; i<32; i++)
	session_key[i] = random_byte();

    len = (hostkey.bytes > servkey.bytes ? hostkey.bytes : servkey.bytes);

    rsabuf = malloc(len);
    if (!rsabuf)
	fatalbox("Out of memory");

    verify_ssh_host_key(savedhost, &hostkey);

    for (i=0; i<32; i++) {
	rsabuf[i] = session_key[i];
	if (i < 16)
	    rsabuf[i] ^= session_id[i];
    }

    if (hostkey.bytes > servkey.bytes) {
	rsaencrypt(rsabuf, 32, &servkey);
	rsaencrypt(rsabuf, servkey.bytes, &hostkey);
    } else {
	rsaencrypt(rsabuf, 32, &hostkey);
	rsaencrypt(rsabuf, hostkey.bytes, &servkey);
    }

    cipher_type = cfg.cipher == CIPHER_BLOWFISH ? SSH_CIPHER_BLOWFISH :
                  SSH_CIPHER_3DES;
    if ((supported_ciphers_mask & (1 << cipher_type)) == 0) {
	c_write("Selected cipher not supported, falling back to 3DES\r\n", 53);
	cipher_type = SSH_CIPHER_3DES;
    }

    s_wrpkt_start(3, len+15);
    sp->pktout.body[0] = cipher_type;
    memcpy(sp->pktout.body+1, cookie, 8);
    sp->pktout.body[9] = (len*8) >> 8;
    sp->pktout.body[10] = (len*8) & 0xFF;
    memcpy(sp->pktout.body+11, rsabuf, len);
    sp->pktout.body[len+11] = sp->pktout.body[len+12] = 0;   /* protocol flags */
    sp->pktout.body[len+13] = sp->pktout.body[len+14] = 0;
    s_wrpkt();

    free(rsabuf);

    cipher = cipher_type == SSH_CIPHER_BLOWFISH ? &ssh_blowfish :
             &ssh_3des;
    cipher->sesskey(session_key);

    do { crReturnV(sp->pr_line); } while (!ispkt);

    if (sp->pktin.type != 14)
	fatalbox("Encryption not successfully enabled");

    fflush(stdout);
    {
	static char username[100];
	static int pos = 0;
	static char c;
	if (!*cfg.username) {
	    c_write("login as: ", 10);
	    while (pos >= 0) {
		do { crReturnV(sp->pr_line); } while (ispkt);
		while (inlen--) switch (c = *in++) {
		  case 10: case 13:
		    username[pos] = 0;
		    pos = -1;
		    break;
		  case 8: case 127:
		    if (pos > 0) {
			c_write("\b \b", 3);
			pos--;
		    }
		    break;
		  case 21: case 27:
		    while (pos > 0) {
			c_write("\b \b", 3);
			pos--;
		    }
		    break;
		  case 3: case 4:
		    random_save_seed();
		    exit(0);
		    break;
		  default:
		    if (c >= ' ' && c <= '~' && pos < 40) {
			username[pos++] = c;
			c_write(&c, 1);
		    }
		    break;
		}
	    }
	    c_write("\r\n", 2);
	    username[strcspn(username, "\n\r")] = '\0';
	} else {
	    char stuff[200];
	    strncpy(username, cfg.username, 99);
	    username[99] = '\0';
	    sprintf(stuff, "Sent username \"%s\".\r\n", username);
	    c_write(stuff, strlen(stuff));
	}
	s_wrpkt_start(4, 4+strlen(username));
	sp->pktout.body[0] = sp->pktout.body[1] = sp->pktout.body[2] = 0;
	sp->pktout.body[3] = strlen(username);
	memcpy(sp->pktout.body+4, username, strlen(username));
	s_wrpkt();
    }

    do { crReturnV(sp->pr_line); } while (!ispkt);

    while (sp->pktin.type == 15) {
	static char password[100];
	static int pos;
	static char c;
	c_write("password: ", 10);
	pos = 0;
	while (pos >= 0) {
	    do { crReturnV(sp->pr_line); } while (ispkt);
	    while (inlen--) switch (c = *in++) {
	      case 10: case 13:
		password[pos] = 0;
		pos = -1;
		break;
	      case 8: case 127:
		if (pos > 0)
		    pos--;
		break;
	      case 21: case 27:
		pos = 0;
		break;
	      case 3: case 4:
		random_save_seed();
		exit(0);
		break;
	      default:
		if (c >= ' ' && c <= '~' && pos < 40)
		    password[pos++] = c;
		break;
	    }
	}
	c_write("\r\n", 2);
	s_wrpkt_start(9, 4+strlen(password));
	sp->pktout.body[0] = sp->pktout.body[1] = sp->pktout.body[2] = 0;
	sp->pktout.body[3] = strlen(password);
	memcpy(sp->pktout.body+4, password, strlen(password));
	s_wrpkt();
	memset(password, 0, strlen(password));
	do { crReturnV(sp->pr_line); } while (!ispkt);
	if (sp->pktin.type == 15) {
	    c_write("Access denied\r\n", 15);
	} else if (sp->pktin.type != 14) {
	    fatalbox("Strange packet received, type %d", sp->pktin.type);
	}
    }

    if (!cfg.nopty) {
        i = strlen(cfg.termtype);
        s_wrpkt_start(10, i+5*4+1);
        sp->pktout.body[0] = (i >> 24) & 0xFF;
        sp->pktout.body[1] = (i >> 16) & 0xFF;
        sp->pktout.body[2] = (i >> 8) & 0xFF;
        sp->pktout.body[3] = i & 0xFF;
        memcpy(sp->pktout.body+4, cfg.termtype, i);
        i += 4;
        sp->pktout.body[i++] = (rows >> 24) & 0xFF;
        sp->pktout.body[i++] = (rows >> 16) & 0xFF;
        sp->pktout.body[i++] = (rows >> 8) & 0xFF;
        sp->pktout.body[i++] = rows & 0xFF;
        sp->pktout.body[i++] = (cols >> 24) & 0xFF;
        sp->pktout.body[i++] = (cols >> 16) & 0xFF;
        sp->pktout.body[i++] = (cols >> 8) & 0xFF;
        sp->pktout.body[i++] = cols & 0xFF;
        memset(sp->pktout.body+i, 0, 9);       /* 0 pixwidth, 0 pixheight, 0.b endofopt */
        s_wrpkt();
        ssh_state = SSH_STATE_INTERMED;
        do { crReturnV(sp->pr_line); } while (!ispkt);
        if (sp->pktin.type != 14 && sp->pktin.type != 15) {
            fatalbox("Protocol confusion");
        } else if (sp->pktin.type == 15) {
            c_write("Server refused to allocate pty\r\n", 32);
        }
    }

    s_wrpkt_start(12, 0);
    s_wrpkt();

    ssh_state = SSH_STATE_SESSION;
    if (size_needed)
	ssh_size();

    while (1) {
	crReturnV(sp->pr_line);
	if (ispkt) {
	    if (sp->pktin.type == 17 || sp->pktin.type == 18) {
		long len = 0;
		for (i = 0; i < 4; i++)
		    len = (len << 8) + sp->pktin.body[i];
		c_write(sp->pktin.body+4, len);
	    } else if (sp->pktin.type == 1) {
		/* SSH_MSG_DISCONNECT */
                ssh_state = SSH_STATE_CLOSED;
	    } else if (sp->pktin.type == 14) {
		/* SSH_MSG_SUCCESS: may be from EXEC_SHELL on some servers */
	    } else if (sp->pktin.type == 15) {
		/* SSH_MSG_FAILURE: may be from EXEC_SHELL on some servers
		 * if no pty is available or in other odd cases. Ignore */
	    } else if (sp->pktin.type == 20) {
		/* EXITSTATUS */
		s_wrpkt_start(33, 0);
		s_wrpkt();
	    } else {
		fatalbox("Strange packet received: type %d", sp->pktin.type);
	    }
	} else {
	    s_wrpkt_start(16, 4+inlen);
	    sp->pktout.body[0] = (inlen >> 24) & 0xFF;
	    sp->pktout.body[1] = (inlen >> 16) & 0xFF;
	    sp->pktout.body[2] = (inlen >> 8) & 0xFF;
	    sp->pktout.body[3] = inlen & 0xFF;
	    memcpy(sp->pktout.body+4, in, inlen);
	    s_wrpkt();
	}
    }

    crFinishV;
}

/*
 * Called to set up the connection. Will arrange for WM_NETEVENT
 * messages to be passed to the specified window, whose window
 * procedure should then call telnet_msg().
 *
 * Returns an error message, or NULL on success.
 *
 * Also places the canonical host name into `realhost'.
 */
static char *ssh_init(Session *sess) {
    struct ssh_private *sp;
    char *host = sess->cfg.host;
    int port = sess->cfg.port;

    sess->back_priv = smalloc(sizeof(struct ssh_private));
    sp = (struct ssh_private *)sess->back_priv;
    memset(*sp, 0, sizeof(*sp));
    sp->s = INVALID_SOCKET;
    sp->ssh_state = SSH_STATE_BEFORE_SIZE;

    sp->savedhost = smalloc(1+strlen(host));
    strcpy(sp->savedhost, host);

#ifdef FWHACK
    sp->FWhost = host;
    sp->FWport = port;
    host = FWSTR;
    port = 23;
#endif

    if (port < 0)
	port = 22;		       /* default ssh port */
    sp->s = net_open(sess, host, port); 

    return NULL;
}

static void ssh_opened(Session *sess) {
    struct ssh_private *sp = (struct ssh_private *)sess->back_priv;

#ifdef FWHACK
    send(sp->s, "connect ", 8, 0);
    send(sp->s, FWhost, strlen(FWhost), 0);
    {
	char buf[20];
	sprintf(buf, " %d\n", FWport);
	send (s, buf, strlen(buf), 0);
    }
#endif

    if (!do_ssh_init())
	return "Protocol initialisation error";

    if (WSAAsyncSelect (s, hwnd, WM_NETEVENT, FD_READ | FD_CLOSE) == SOCKET_ERROR)
	switch (WSAGetLastError()) {
	  case WSAENETDOWN: return "Network is down";
	  default: return "WSAAsyncSelect(): unknown error";
	}

    return NULL;
}

/*
 * Process a WM_NETEVENT message. Will return 0 if the connection
 * has closed, or <0 for a socket error.
 */
static int ssh_msg (Session *sess, SOCKET sock, Net_Event_Type ne) {
    struct ssh_private *sp = (struct ssh_private *)sess->back_priv;
    int ret;
    char buf[256];

    if (s == INVALID_SOCKET)	       /* how the hell did we get here?! */
	return -5000;

    switch (ne) {
      case NE_OPEN:
        ssh_opened(sess);
        return 1;
      case NE_DATA:
	ret = net_recv(sock, buf, sizeof(buf), 0);
	if (ret < 0)		       /* any _other_ error */
	    return -1;
	if (ret == 0) {
	    sp->s = INVALID_SOCKET;
	    return 0;		       /* can't happen, in theory */
	}
	ssh_gotdata(sess, buf, ret);
	return 1;
      case NE_CLOSING:
	sp->s = INVALID_SOCKET;
        sp->ssh_state = SSH_STATE_CLOSED;
	return 0;
      case NE_NOHOST:
        fatalbox("Host not found");
      case NE_REFUSED:
        fatalbox("Connection refused");
      case NE_NOOPEN:
        fatalbox("Unable to open connection");
      case NE_TIMEOUT:
        fatalbox("Connection timed out");
      case NE_ABORT:
        fatalbox("Connection reset by peer");
      case NE_DIED:
        fatalbox("Connection died");
    }
    return 1;			       /* shouldn't happen, but WTF */
}

/*
 * Called to send data down the Telnet connection.
 */
static void ssh_send(Session *sess, char *buf, int len) {
    if (s == INVALID_SOCKET)
	return;

    ssh_protocol(sess, buf, len, 0);
}

/*
 * Called to set the size of the window from Telnet's POV.
 */
static void ssh_size(Session *sess) {
    struct ssh_private *sp = (struct ssh_private *sp)sess->back_priv;

    switch (sp->ssh_state) {
      case SSH_STATE_BEFORE_SIZE:
      case SSH_STATE_CLOSED:
	break;			       /* do nothing */
      case SSH_STATE_INTERMED:
	sp->size_needed = TRUE;	       /* buffer for later */
	break;
      case SSH_STATE_SESSION:
        if (!sess->cfg.nopty) {
            s_wrpkt_start(11, 16);
            sp->pktout.body[0] = (rows >> 24) & 0xFF;
            sp->pktout.body[1] = (rows >> 16) & 0xFF;
            sp->pktout.body[2] = (rows >> 8) & 0xFF;
            sp->pktout.body[3] = rows & 0xFF;
            sp->pktout.body[4] = (cols >> 24) & 0xFF;
            sp->pktout.body[5] = (cols >> 16) & 0xFF;
            sp->pktout.body[6] = (cols >> 8) & 0xFF;
            sp->pktout.body[7] = cols & 0xFF;
            memset(sp->pktout.body+8, 0, 8);
            s_wrpkt();
        }
    }
}

/*
 * (Send Telnet special codes)
 */
static void ssh_special (Telnet_Special code) {
    /* do nothing */
}

Backend ssh_backend = {
    ssh_init,
    ssh_msg,
    ssh_send,
    ssh_size,
    ssh_special
};

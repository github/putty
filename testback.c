/* $Id: testback.c,v 1.1.2.7 1999/08/02 08:04:31 ben Exp $ */
/*
 * Copyright (c) 1999 Simon Tatham
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

/* PuTTY test backends */

#include <stdio.h>
#include <stdlib.h>

#include "putty.h"

static char *null_init(Session *);
static int null_msg(Session *, SOCKET, Net_Event_Type);
static void null_send(Session *, char *, int);
static void loop_send(Session *, char *, int);
static void hexdump_send(Session *, char *, int);
static void null_size(Session *);
static void null_special(Session *, Telnet_Special);

Backend null_backend = {
    null_init, null_msg, null_send, null_size, null_special
};

Backend loop_backend = {
    null_init, null_msg, loop_send, null_size, null_special
};

Backend hexdump_backend = {
    null_init, null_msg, hexdump_send, null_size, null_special
};

static char *null_init(Session *s) {

    return NULL;
}

static int null_msg(Session *s, SOCKET sock, Net_Event_Type ne) {

    return 1;
}

static void null_send(Session *s, char *buf, int len) {

}

static void loop_send (Session *s, char *buf, int len) {

    while (len--) {
	int new_head = (s->inbuf_head + 1) & INBUF_MASK;
	int c = (unsigned char) *buf;
	if (new_head != s->inbuf_reap) {
	    s->inbuf[s->inbuf_head] = *buf++;
	    s->inbuf_head = new_head;
	}
    }
    term_out(s);
    term_update(s);
}

static void hexdump_send(Session *s, char *buf, int len) {
    static char mybuf[10];
    int mylen;

    while (len--) {
	mylen = sprintf(mybuf, "%02x\015\012", (unsigned char)*buf++);
	loop_send(s, mybuf, mylen);
    }
}

static void null_size(Session *s) {

}

static void null_special(Session *s, Telnet_Special code) {

}

/*
 * Emacs magic:
 * Local Variables:
 * c-file-style: "simon"
 * End:
 */

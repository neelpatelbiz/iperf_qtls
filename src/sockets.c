/*---------------------------------------------------------------
 * Copyright (c) 1999,2000,2001,2002,2003
 * The Board of Trustees of the University of Illinois
 * All Rights Reserved.
 *---------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software (Iperf) and associated
 * documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 *
 * Redistributions of source code must retain the above
 * copyright notice, this list of conditions and
 * the following disclaimers.
 *
 *
 * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimers in the documentation and/or other materials
 * provided with the distribution.
 *
 *
 * Neither the names of the University of Illinois, NCSA,
 * nor the names of its contributors may be used to endorse
 * or promote products derived from this Software without
 * specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE CONTIBUTORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ________________________________________________________________
 * National Laboratory for Applied Network Research
 * National Center for Supercomputing Applications
 * University of Illinois at Urbana-Champaign
 * http://www.ncsa.uiuc.edu
 * ________________________________________________________________
 *
 * socket.c
 * by Mark Gates <mgates@nlanr.net>
 * -------------------------------------------------------------------
 * set/getsockopt and read/write wrappers
 * ------------------------------------------------------------------- */

#include "headers.h"
#include "util.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------
 * If inMSS > 0, set the TCP maximum segment size  for inSock.
 * Otherwise leave it as the system default.
 * ------------------------------------------------------------------- */

const char warn_mss_fail[] = "\
WARNING: attempt to set TCP maxmimum segment size to %d failed.\n\
Setting the MSS may not be implemented on this OS.\n";

const char warn_mss_notset[] =
"WARNING: attempt to set TCP maximum segment size to %d, but got %d\n";

void setsock_tcp_mss (int inSock, int inMSS) {
#ifdef TCP_MAXSEG
    int rc;
    int newMSS;
    Socklen_t len;

    assert(inSock != INVALID_SOCKET);

    if (inMSS > 0) {
        /* set */
        newMSS = inMSS;
        len = sizeof(newMSS);
        rc = setsockopt(inSock, IPPROTO_TCP, TCP_MAXSEG, (char*) &newMSS,  len);
        if (rc == SOCKET_ERROR) {
            fprintf(stderr, warn_mss_fail, newMSS);
            return;
        }

        /* verify results */
        rc = getsockopt(inSock, IPPROTO_TCP, TCP_MAXSEG, (char*) &newMSS, &len);
        WARN_errno(rc == SOCKET_ERROR, "getsockopt TCP_MAXSEG");
        if (newMSS != inMSS) {
            fprintf(stderr, warn_mss_notset, inMSS, newMSS);
        }
    }
#endif
} /* end setsock_tcp_mss */

/* -------------------------------------------------------------------
 * returns the TCP maximum segment size
 * ------------------------------------------------------------------- */

int getsock_tcp_mss  (int inSock) {
    int theMSS = -1;
#ifdef TCP_MAXSEG
    int rc;
    Socklen_t len;
    assert(inSock >= 0);

    /* query for MSS */
    len = sizeof(theMSS);
    rc = getsockopt(inSock, IPPROTO_TCP, TCP_MAXSEG, (char*)&theMSS, &len);
    WARN_errno(rc == SOCKET_ERROR, "getsockopt TCP_MAXSEG");
#endif
    return theMSS;
} /* end getsock_tcp_mss */

/* -------------------------------------------------------------------
 * Attempts to reads n bytes from a socket.
 * Returns number actually read, or -1 on error.
 * If number read < inLen then we reached EOF.
 *
 * from Stevens, 1998, section 3.9
 * ------------------------------------------------------------------- */
ssize_t readn (int inSock, void *outBuf, size_t inLen) {
    size_t  nleft;
    ssize_t nread;
    char *ptr;

    assert(inSock >= 0);
    assert(outBuf != NULL);
    assert(inLen > 0);

    ptr   = (char*) outBuf;
    nleft = inLen;

    while (nleft > 0) {
        nread = read(inSock, ptr, nleft);
        if (nread < 0) {
            if (errno == EINTR)
                nread = 0;  /* interupted, call read again */
            else
                return -1;  /* error */
        } else if (nread == 0)
            break;        /* EOF */

        nleft -= nread;
        ptr   += nread;
    }

    return(inLen - nleft);
} /* end readn */

/* -------------------------------------------------------------------
 * Similar to read but supports recv flags
 * Returns number actually read, or -1 on error.
 * If number read < inLen then we reached EOF.
 * from Stevens, 1998, section 3.9
 * ------------------------------------------------------------------- */
int recvn (int inSock, void *conn, char *outBuf, int inLen, int flags) {
    int  nleft;
    int nread = 0;
    char *ptr;

    assert(inSock >= 0);
    assert(outBuf != NULL);
    assert(inLen > 0);

    ptr   = outBuf;
    nleft = inLen;
#if (HAVE_DECL_MSG_PEEK)
    if (flags & MSG_PEEK) {
	while (nleft != nread) {
	    if (conn != NULL)
		nread = SSL_read(conn, ptr, nleft);
	    else
	        nread = recv(inSock, ptr, nleft, flags);
	    switch (nread) {
	    case SOCKET_ERROR :
		// Note: use TCP fatal error codes even for UDP
		if (FATALTCPREADERR(errno)) {
		    WARN_errno(1, "recvn peek");
		    nread = -1;
		    goto DONE;
		}
#ifdef HAVE_THREAD_DEBUG
		WARN_errno(1, "recvn peek non-fatal");
#endif
		break;
	    case 0:
#ifdef HAVE_THREAD_DEBUG
		WARN(1, "recvn peek peer close");
#endif
		goto DONE;
		break;
	    default :
		break;
	    }
	}
    } else
#endif
    {
	while (nleft >  0) {
	    if (conn != NULL) {
		nread = SSL_read(conn, ptr, nleft);
	    } else {
#if (HAVE_DECL_MSG_WAITALL)
		nread = recv(inSock, ptr, nleft, MSG_WAITALL);
#else
		nread = recv(inSock, ptr, nleft, 0);
#endif
	    }
	    switch (nread) {
	    case SOCKET_ERROR :
		// Note: use TCP fatal error codes even for UDP
		if (FATALTCPREADERR(errno)) {
		    WARN_errno(1, "recvn");
		    nread = -1;
		    goto DONE;
		}
#ifdef HAVE_THREAD_DEBUG
		WARN_errno(1, "recvn non-fatal");
#endif
		break;
	    case 0:
#ifdef HAVE_THREAD_DEBUG
		WARN(1, "recvn peer close");
#endif
		nread = inLen - nleft;
		goto DONE;
		break;
	    default :
		nleft -= nread;
		ptr   += nread;
		break;
	    }
	    nread = inLen - nleft;
	}
    }
  DONE:
    return(nread);
} /* end readn */

/* -------------------------------------------------------------------
 * Attempts to write  n bytes to a socket.
 * returns number actually written, or -1 on error.
 * number written is always inLen if there is not an error.
 *
 * from Stevens, 1998, section 3.9
 * ------------------------------------------------------------------- */

int writen (int inSock, void *conn, const void *inBuf, int inLen, int *count) {
    int nleft;
    int nwritten;
    const char *ptr;

    assert(inSock >= 0);
    assert(inBuf != NULL);
    assert(inLen > 0);

    ptr   = (char*) inBuf;
    nleft = inLen;
    nwritten = 0;
    *count = 0;

    while (nleft > 0) {
	if (conn != NULL)
		nwritten = SSL_write(conn, ptr, nleft);
	else
	        nwritten = write(inSock, ptr, nleft);
	(*count)++;
	switch (nwritten) {
	case SOCKET_ERROR :
	    if (!(errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK)) {
		nwritten = inLen - nleft;
		WARN_errno(1, "writen fatal");
		goto DONE;
	    }
	    break;
	case 0:
	    // write timeout - retry
	    break;
	default :
	    nleft -= nwritten;
	    ptr   += nwritten;
	    break;
	}
	nwritten = inLen - nleft;
    }
  DONE:
    return (nwritten);
} /* end writen */


/*
 * Set a socket to blocking or non-blocking
*
 * Returns true on success, or false if there was an error
*/
#define FALSE 0
#define TRUE 1
bool setsock_blocking (int fd, bool blocking) {
   if (fd < 0) return FALSE;

#ifdef WIN32
   unsigned long mode = blocking ? 0 : 1;
   return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? TRUE : FALSE;
#else
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags < 0) return FALSE;
   flags = blocking ? (flags&~O_NONBLOCK) : (flags|O_NONBLOCK);
   return (fcntl(fd, F_SETFL, flags) == 0) ? TRUE : FALSE;
#endif
}


#ifdef __cplusplus
} /* end extern "C" */
#endif

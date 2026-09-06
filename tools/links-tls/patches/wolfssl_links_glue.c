/*
 * wolfssl_links_glue.c -- Watt-32 user-IO callbacks for Links + wolfSSL
 * on DJGPP (wolfSSL built with WOLFSSL_NO_SOCK / WOLFSSL_USER_IO).
 */
#include "links.h"
#ifdef HAVE_SSL

#include <errno.h>
#include <sys/socket.h>
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

/* open/write/close each call: DJGPP buffers otherwise die with QEMU */
static void tlog(const char *msg)
{
	FILE *f = fopen("C:\\TLSGLUE.TXT", "a");
	if (f) { fputs(msg, f); fclose(f); }
}

void wolfssl_tlog(const char *msg)
{
	tlog(msg);
}

void wolfssl_dbg_hello(int maj, int min, int sz, unsigned long mask, int mindg, int dg)
{
	char mbuf[96];
	sprintf(mbuf, "HELLO_DBG: ver=%d.%02x suites=%d mask=%08lx mindg=%d dg=%d\n", maj, min, sz, mask, mindg, dg);
	tlog(mbuf);
}

int wolfssl_links_cb_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
	int r;
	r = recv((int)(long)ctx, buf, sz, 0);
	if (r == 0)
		return WOLFSSL_CBIO_ERR_CONN_CLOSE;  /* EOF: peer closed (no close_notify) */
	if (r < 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
			return WOLFSSL_CBIO_ERR_WANT_READ;
		return WOLFSSL_CBIO_ERR_GENERAL;
	}
	return r;
}

int wolfssl_links_cb_send(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
	int r;
	r = send((int)(long)ctx, buf, sz, 0);
	if (r < 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
			return WOLFSSL_CBIO_ERR_WANT_WRITE;
		return WOLFSSL_CBIO_ERR_GENERAL;
	}
	return r;
}

/* wolfSSL compat lacks SSL_get_cipher_bits; parse it from the suite name */
int SSL_get_cipher_bits(WOLFSSL *ssl, int *algkeysize)
{
	const char *n = wolfSSL_get_cipher_name(ssl);
	int bits = 128;
	if (n) {
		if (strstr(n, "256")) bits = 256;
		else if (strstr(n, "128")) bits = 128;
		else if (strstr(n, "CHACHA")) bits = 256;
	}
	if (algkeysize) *algkeysize = bits;
	return bits;
}

/* ---- hardware diagnostics: socket + SSL failure counters ---- */
long sock_open_count, sock_close_count;
void sock_log2(const char *ev)
{
	FILE *f = fopen("C:\\SOCKSTAT.LOG", "a");
	if (f) { fputs(ev, f); fputc('\n', f); fclose(f); }
}

void sock_log(const char *ev)
{
	FILE *f = fopen("C:\\SOCKSTAT.LOG", "a");
	if (f) {
		fprintf(f, "%s open=%ld close=%ld\n", ev, sock_open_count, sock_close_count);
		fclose(f);
	}
}

void wolfssl_links_install_io(WOLFSSL_CTX *ctx)
{
	{
		char mbuf[96];
		sprintf(mbuf, "objsize: SSL=%d CTX=%d METHOD=%d\n",
			wolfSSL_GetObjectSize(), wolfSSL_CTX_GetObjectSize(), wolfSSL_METHOD_GetObjectSize());
		tlog(mbuf);
	}
	tlog("install_io enter\n");
	wolfSSL_CTX_SetIORecv(ctx, wolfssl_links_cb_recv);
	wolfSSL_CTX_SetIOSend(ctx, wolfssl_links_cb_send);
}

/* ------------------------------------------------------------------ */
/* qjs_http_request: blocking HTTP/1.1 client for QuickJS fetch().
 *
 * Absolute URL in, full response (status line + headers + body) out.
 * Returns 0 on success, -1 on failure (message logged to SOCKSTAT).
 * Uses Links' own getSSL() context (SNI, CA bundle, TLS 1.3) for https.
 */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define QJS_HTTP_TIMEOUT_SEC 45

static int qjs_http_select_read(int sock, int sec)
{
	fd_set rf;
	struct timeval tv;
	FD_ZERO(&rf);
	FD_SET(sock, &rf);
	tv.tv_sec = sec;
	tv.tv_usec = 0;
	return select(sock + 1, &rf, NULL, NULL, &tv);
}

int qjs_http_request(const char *url, const char *method,
		     const char *content_type, const char *body,
		     const char *user_agent,
		     char **out, long *outlen, int *status)
{
	char host[256], portstr[8];
	const char *path, *p, *hs;
	int https = 0, port = 0;
	int sock = -1, r, rc = -1;
	struct hostent *he;
	struct sockaddr_in sa;
	char req[2048];
	char *resp = NULL;
	long resplen = 0, respalloc = 0;
	links_ssl *ls = NULL;

	*out = NULL;
	*outlen = 0;
	*status = 0;

	if (!strncasecmp(url, "https://", 8)) { https = 1; hs = url + 8; }
	else if (!strncasecmp(url, "http://", 7)) hs = url + 7;
	else return -1;

	p = hs;
	while (*p && *p != '/' && *p != '?' && *p != '#') p++;
	{
		size_t hl = (size_t)(p - hs);
		const char *colon = memchr(hs, ':', hl);
		if (colon) {
			if (hl - (size_t)(colon - hs) >= sizeof(host)) return -1;
			memcpy(host, hs, colon - hs);
			host[colon - hs] = 0;
			port = atoi(colon + 1);
		} else {
			if (hl >= sizeof(host)) return -1;
			memcpy(host, hs, hl);
			host[hl] = 0;
		}
	}
	if (!port) port = https ? 443 : 80;
	sprintf(portstr, "%d", port);
	path = (*p == '/') ? p : "/";
	if (!*p) path = "/";

	he = gethostbyname(host);
	if (!he || he->h_addrtype != AF_INET) {
		sock_log2("FETCH: dns fail");
		return -1;
	}
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)port);
	memcpy(&sa.sin_addr, he->h_addr_list[0], 4);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) { sock_log2("FETCH: socket fail"); return -1; }
	sock_open_count++;
	if (connect(sock, (struct sockaddr *)&sa, sizeof sa) < 0) {
		sock_log2("FETCH: connect fail");
		goto out;
	}

	if (https) {
		ls = getSSL();
		if (!ls) { sock_log2("FETCH: getSSL fail"); goto out; }
		SSL_set_fd(ls->ssl, sock);
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
		SSL_set_tlsext_host_name(ls->ssl, host);
#endif
		r = SSL_connect(ls->ssl);
		if (r != 1) {
			char m[96];
			sprintf(m, "FETCH: ssl_connect fail ret=%d err=%d", r, (int)ERR_get_error());
			sock_log2(m);
			goto out;
		}
	}

	{
		int off = snprintf(req, sizeof req,
			"%s %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"User-Agent: %s\r\n"
			"Accept: */*\r\n"
			"Connection: close\r\n",
			method ? method : "GET", path, host,
			user_agent ? user_agent : "Links");
		if (content_type && body)
			off += snprintf(req + off, sizeof req - off,
				"Content-Type: %s\r\nContent-Length: %lu\r\n",
				content_type, (unsigned long)strlen(body));
		if (off > 0 && off < (int)sizeof req - 4) {
			memcpy(req + off, "\r\n", 2);
			off += 2;
		} else off = -1;
		if (off < 0) { sock_log2("FETCH: request too long"); goto out; }
		if (body && (size_t)off + strlen(body) < sizeof req)
			strcpy(req + off, body);
		else if (body) { sock_log2("FETCH: body too long"); goto out; }

		if (ls) {
			/* loop SSL_write: handles partial writes */
			int total = (int)(off + (body ? strlen(body) : 0)), sent = 0;
			while (sent < total) {
				int w = SSL_write(ls->ssl, req + sent, total - sent);
				if (w <= 0) { sock_log2("FETCH: ssl write fail"); goto out; }
				sent += w;
			}
		} else {
			if (send(sock, req, (int)(off + (body ? strlen(body) : 0)), 0) < 0)
				goto out;
		}
	}

	/* read the whole response */
	for (;;) {
		char buf[4096];
		int n;
		if (qjs_http_select_read(sock, QJS_HTTP_TIMEOUT_SEC) <= 0) {
			sock_log2("FETCH: timeout");
			goto out;
		}
		if (ls)
			n = SSL_read(ls->ssl, buf, sizeof buf);
		else
			n = recv(sock, buf, sizeof buf, 0);
		if (n <= 0)
			break;
		if (resplen + n > respalloc) {
			char *nr;
			while (resplen + n > respalloc) respalloc = respalloc ? respalloc * 2 : 16384;
			nr = realloc(resp, respalloc);
			if (!nr) { sock_log2("FETCH: oom"); goto out; }
			resp = nr;
		}
		memcpy(resp + resplen, buf, n);
		resplen += n;
		if (resplen > 8L * 1024 * 1024) { sock_log2("FETCH: too big"); goto out; }
	}
	if (!resplen) goto out;
	/* status: "HTTP/1.x NNN ..." */
	if (!strncmp(resp, "HTTP/", 5)) {
		*status = atoi(resp + 9);
	} else *status = 200;
	{
		char m[256];
		snprintf(m, sizeof m, "FETCH-OK %.200s status=%d bytes=%ld",
			url, *status, resplen);
		sock_log2(m);
	}
	*out = resp;
	*outlen = resplen;
	rc = 0;
	resp = NULL;
out:
	free(resp);
	if (ls) freeSSL(ls);
	if (sock >= 0) { closesocket(sock); sock_close_count++; }
	return rc;
}

#endif /* HAVE_SSL */

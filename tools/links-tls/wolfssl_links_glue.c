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

static FILE *g_tlog;
static void tlog(const char *msg)
{
	if (!g_tlog) g_tlog = fopen("C:\\TLSGLUE.TXT", "a");
	if (g_tlog) { fputs(msg, g_tlog); fflush(g_tlog); }
}

int wolfssl_links_cb_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
	tlog("cb_recv enter\n");
	int r = recv((int)(long)ctx, buf, sz, 0);
	if (r < 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
			return WOLFSSL_CBIO_ERR_WANT_READ;
		return WOLFSSL_CBIO_ERR_GENERAL;
	}
	return r;
}

int wolfssl_links_cb_send(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
	tlog("cb_send enter\n");
	int r = send((int)(long)ctx, buf, sz, 0);
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

/* Dedicated bump arena for wolfSSL: avoids DJGPP malloc/sbrk during the
 * handshake (workaround for the _extendsbrk page fault seen in Links). */
#define WARENA_SIZE (3 * 1024 * 1024)
static unsigned char *warena;
static size_t warena_used;

static void *wa_malloc(size_t n)
{
	void *p;
	if (!warena) {
		warena = malloc(WARENA_SIZE);
		if (!warena) return NULL;
	}
	n = (n + 15) & ~(size_t)15;
	if (warena_used + n > WARENA_SIZE) return NULL;
	p = warena + warena_used;
	warena_used += n;
	return p;
}

static void wa_free(void *p)
{
	(void)p;  /* arena is only released at exit */
}

static void *wa_realloc(void *p, size_t n)
{
	/* bump alloc + copy: old block size is unknown, so copy generously */
	void *q = wa_malloc(n);
	if (q && p) memmove(q, p, n);
	return q;
}

void wolfssl_links_install_io(WOLFSSL_CTX *ctx)
{
	wolfSSL_SetAllocators(wa_malloc, wa_free, wa_realloc);
	tlog("install_io enter\n");
	wolfSSL_CTX_SetIORecv(ctx, wolfssl_links_cb_recv);
	wolfSSL_CTX_SetIOSend(ctx, wolfssl_links_cb_send);
}

#endif /* HAVE_SSL */

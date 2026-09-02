/*
 * TLSTEST.EXE -- DOS TLS 1.3 client smoke test: wolfSSL 5.8 (DJGPP) over
 * Watt-32 sockets via user-IO callbacks. Connects to 10.0.2.2:8443,
 * performs the handshake, sends "GET / HTTP/1.0" and prints the response
 * marker TLS13-DOS-OK-42 on success.
 */
#include <stdio.h>
#include <string.h>
#include <tcp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

static int cb_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
	int r = recv((int)(long)ctx, buf, sz, 0);
	if (r < 0) printf("cb_recv: r=%d errno=%d\n", r, errno);
	return r < 0 ? WOLFSSL_CBIO_ERR_GENERAL : r;
}

static int cb_send(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
	int r = send((int)(long)ctx, buf, sz, 0);
	if (r < 0) printf("cb_send: r=%d errno=%d\n", r, errno);
	return r < 0 ? WOLFSSL_CBIO_ERR_GENERAL : r;
}

static void *my_malloc(size_t n) { void *p = malloc(n); if (!p) printf("XMALLOC failed (%u)\n", (unsigned)n); return p; }
static void my_free(void *p) { free(p); }
static void *my_realloc(void *p, size_t n) { void *q = realloc(p, n); if (!q) printf("REALLOC failed (%u)\n", (unsigned)n); return q; }

int main(void)
{
	WOLFSSL_CTX *ctx;
	WOLFSSL *ssl;
	int sock, rc;
	char buf[512];
	static char req[] = "GET / HTTP/1.0\r\nHost: 10.0.2.2\r\n\r\n";

	if (sock_init()) {
		puts("sock_init failed");
		return 1;
	}
	puts("watt32 up");

	wolfSSL_Init();
	wolfSSL_SetAllocators(my_malloc, my_free, my_realloc);
	wolfSSL_Debugging_ON();
	{
		WOLFSSL_METHOD *m = (WOLFSSL_METHOD *)wolfTLSv1_3_client_method();
		WOLFSSL_METHOD *m23 = (WOLFSSL_METHOD *)wolfSSLv23_client_method();
		printf("method13=%p method23=%p\n", (void*)m, (void*)m23);
		ctx = wolfSSL_CTX_new(m23 ? m23 : m);
	}
	if (!ctx) { puts("ctx_new failed"); wolfSSL_ERR_print_errors_fp(stdout, 0); puts(""); return 1; }
	wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, 0);
	wolfSSL_CTX_SetIORecv(ctx, cb_recv);
	wolfSSL_CTX_SetIOSend(ctx, cb_send);
	puts("ctx ready");

	sock = socket(AF_INET, SOCK_STREAM, 0);
	{
		struct sockaddr_in a;
		memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET;
		a.sin_port = htons(8443);
		a.sin_addr.s_addr = inet_addr("10.0.2.2");
		if (connect(sock, (struct sockaddr *)&a, sizeof(a))) {
			puts("connect failed");
			return 1;
		}
	}
	puts("tcp connected");

	ssl = wolfSSL_new(ctx);
	if (!ssl) { puts("ssl_new failed"); return 1; }
	wolfSSL_SetIOReadCtx(ssl, (void *)(long)sock);
	wolfSSL_SetIOWriteCtx(ssl, (void *)(long)sock);

	rc = wolfSSL_connect(ssl);
	printf("connect rc=%d err=%d\n", rc, wolfSSL_get_error(ssl, rc));
	if (rc != WOLFSSL_SUCCESS) {
		char e[90];
		wolfSSL_ERR_error_string(wolfSSL_get_error(ssl, rc), e);
		printf("handshake failed: %s\n", e);
		return 1;
	}
	printf("tls version: %s\n", wolfSSL_get_version(ssl));

	wolfSSL_write(ssl, req, (int)strlen(req));
	{
		int total = 0, found = 0, i;
		for (i = 0; i < 10; i++) {
			rc = wolfSSL_read(ssl, buf + total, (int)sizeof(buf) - 1 - total);
			if (rc <= 0) break;
			total += rc;
			buf[total] = 0;
			if (strstr(buf, "TLS13-DOS-OK-42")) { found = 1; break; }
		}
		printf("read %d bytes total\n", total);
		puts(found ? "TLS13-DOS-OK-42 FOUND - SUCCESS" : "marker missing");
	}
	return 0;
}

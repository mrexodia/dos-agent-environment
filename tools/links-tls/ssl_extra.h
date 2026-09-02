/* DJGPP/wolfSSL glue for Links: SSL_set_fd must also bind the user-IO
 * context because wolfSSL was built with WOLFSSL_USER_IO. */
#ifndef LINKS_WOLFSSL_SSL_EXTRA
#define LINKS_WOLFSSL_SSL_EXTRA
#include <wolfssl/options.h>
int wolfssl_links_cb_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx);
int wolfssl_links_cb_send(WOLFSSL *ssl, char *buf, int sz, void *ctx);
void wolfssl_links_install_io(WOLFSSL_CTX *ctx);
#undef SSL_set_fd
#define SSL_set_fd(ssl, fd) (wolfSSL_SetIOReadCtx((ssl), (void *)(long)(fd)), \
			     wolfSSL_SetIOWriteCtx((ssl), (void *)(long)(fd)), 1)
#endif

/*
 * mujs_engine.c -- MuJS-backed implementation of the Links JavaScript
 * engine interface (see mujs_engine.h). The browser-side API (document
 * writing, dialogs, title, location, forms enumeration) is provided by
 * jsint.c's js_upcall_* functions, which we call directly or deferred
 * through install_timer, mirroring the old engine's behaviour.
 */
#include "mujs_engine.h"
#include "struct.h"

static long mujs_context_counter = 0;

/* globals the js_mem_alloc/stracpy1 macros in struct.h reference
 * (originally defined in the old engine's pomocny.c) */
char *js_temp_var_for_stracpy1;
size_t js_js_temp_var2;
void *js_js_temp_var1;
long js_zaflaknuto_pameti;

/* ---------------- DOM natives ---------------- */

static struct javascript_context *ctxof(js_State *J)
{
	return (struct javascript_context *)js_getcontext(J);
}

static void mju_document_write(js_State *J)
{
	struct javascript_context *ctx = ctxof(J);
	int i, n = js_gettop(J);
	for (i = 1; i < n; i++) {
		const char *s = js_tostring(J, i);
		if (s && *s)
			js_upcall_document_write(ctx->ptr, (unsigned char *)s,
						 (int)strlen(s));
	}
	js_pushundefined(J);
}

static void mju_document_writeLn(js_State *J)
{
	mju_document_write(J);
	js_upcall_document_write(ctxof(J)->ptr, (unsigned char *)"\n", 1);
	js_pushundefined(J);
}

static void mju_document_get_cookie(js_State *J)
{
	struct javascript_context *ctx = ctxof(J);
	js_pushstring(J, ctx && ctx->cookies ? (const char *)ctx->cookies : "");
}

static void mju_document_set_cookie(js_State *J)
{
	struct javascript_context *ctx = ctxof(J);
	const char *c = js_tostring(J, -1);
	if (!ctx || !c || !*c)
		return;
	if (ctx->cookies) {
		size_t la = strlen((const char *)ctx->cookies);
		size_t lb = strlen(c);
		unsigned char *joined = (unsigned char *)js_mem_alloc(la + 2 + lb + 1);
		if (joined) {
			memcpy(joined, ctx->cookies, la);
			joined[la] = ';'; joined[la + 1] = ' ';
			memcpy(joined + la + 2, c, lb);
			joined[la + 2 + lb] = 0;
			mem_free(ctx->cookies);
			ctx->cookies = joined;
		}
	} else {
		ctx->cookies = (unsigned char *)js_mem_alloc(strlen(c) + 1);
		if (ctx->cookies) strcpy((char *)ctx->cookies, c);
	}
}

/* Deferred dialog helpers: the packet is freed by jsint after showing. */
static void defer_alert(struct javascript_context *ctx, const char *msg)
{
	struct fax_me_tender_string *fax = mem_alloc(sizeof(struct fax_me_tender_string));
	if (!fax) return;
	fax->ident = ctx->ptr;
	fax->string = stracpy((const unsigned char *)(msg ? msg : ""));
	install_timer(1, (void (*)(void *))js_upcall_alert, fax);
}

static void mju_alert(js_State *J)
{
	defer_alert(ctxof(J), js_tostring(J, 1));
	js_pushundefined(J);
}

/* window.location = url (and location.href): navigate after the script. */
static void mju_location_set(js_State *J)
{
	struct javascript_context *ctx = ctxof(J);
	struct fax_me_tender_string *fax;
	const char *url = js_tostring(J, -1);
	if (ctx && url && *url) {
		fax = mem_alloc(sizeof(struct fax_me_tender_string));
		if (fax) {
			fax->ident = ctx->ptr;
			fax->string = stracpy((const unsigned char *)url);
			js_upcall_goto_url(fax);
		}
	}
	js_pushundefined(J);
}

static void mju_location_get(js_State *J)
{
	unsigned char *url = js_upcall_get_location(ctxof(J)->ptr);
	js_pushstring(J, url ? (const char *)url : "");
	if (url) mem_free(url);
}

/* Minimal read-only document/window façade good enough for the conformance
 * suite and simple pages: title, lastModified, forms.length, referrer. */
static void mju_get_title(js_State *J)
{
	unsigned char *t = js_upcall_get_title(ctxof(J)->ptr);
	js_pushstring(J, t ? (const char *)t : "");
	if (t) mem_free(t);
}

static void mju_get_referrer(js_State *J)
{
	unsigned char *t = js_upcall_get_referrer(ctxof(J)->ptr);
	js_pushstring(J, t ? (const char *)t : "");
	if (t) mem_free(t);
}

static void mju_get_appname(js_State *J)
{
	unsigned char *t = js_upcall_get_appname();
	js_pushstring(J, t ? (const char *)t : "");
	if (t) mem_free(t);
}

static void mju_get_useragent(js_State *J)
{
	unsigned char *t = js_upcall_get_useragent(ctxof(J)->ptr);
	js_pushstring(J, t ? (const char *)t : "");
	if (t) mem_free(t);
}

static void mju_noop(js_State *J)
{
	js_pushundefined(J);
}

/* js_defaccessor requires BOTH a getter and a setter on the stack;
 * read-only properties get this dummy setter. */
static void mju_ro_setter(js_State *J)
{
	js_pushundefined(J);
}

static void mju_get_forms_helper(js_State *J)
{
	/* document.forms object with a length; full enumeration lives in
	 * js_upcall_get_forms and can be wired in later. */
	js_newobject(J);
	js_pushnumber(J, 0);
	js_setproperty(J, -2, "length");
	js_setproperty(J, -2, "forms");
}

static void register_globals(js_State *J)
{
	/* document */
	js_newobject(J);
	js_newcfunction(J, mju_document_write, "write", 0);
	js_setproperty(J, -2, "write");
	js_newcfunction(J, mju_document_writeLn, "writeln", 0);
	js_setproperty(J, -2, "writeln");
	js_newcfunction(J, mju_document_get_cookie, "getCookie", 0);
	js_newcfunction(J, mju_document_set_cookie, "setCookie", 1);
	js_defaccessor(J, -3, "cookie", JS_DONTENUM);
	js_newcfunction(J, mju_get_title, "getTitle", 0);
	js_newcfunction(J, mju_ro_setter, "setTitle", 0);
	js_defaccessor(J, -3, "title", JS_DONTENUM);
	js_newcfunction(J, mju_get_referrer, "getReferrer", 0);
	js_newcfunction(J, mju_ro_setter, "setReferrer", 0);
	js_defaccessor(J, -3, "referrer", JS_DONTENUM);
	mju_get_forms_helper(J);
	js_setglobal(J, "document");

	/* window */
	js_newobject(J);
	js_newcfunction(J, mju_alert, "alert", 1);
	js_setproperty(J, -2, "alert");
	/* location accessor */
	js_newcfunction(J, mju_location_get, "getLocation", 0);
	js_newcfunction(J, mju_location_set, "setLocation", 1);
	js_defaccessor(J, -3, "location", JS_DONTENUM);
	/* navigator-ish info */
	js_newcfunction(J, mju_get_appname, "getAppName", 0);
	js_newcfunction(J, mju_ro_setter, "setAppName", 0);
	js_defaccessor(J, -3, "appName", JS_DONTENUM);
	js_newcfunction(J, mju_get_useragent, "getUserAgent", 0);
	js_newcfunction(J, mju_ro_setter, "setUserAgent", 0);
	js_defaccessor(J, -3, "userAgent", JS_DONTENUM);
	js_newcfunction(J, mju_noop, "noop", 0);
	js_setproperty(J, -2, "setStatus");
	js_setglobal(J, "window");

	/* alias the common globals into the global object itself */
	js_getglobal(J, "window");
	js_getproperty(J, -1, "alert");
	js_setglobal(J, "alert");
	js_pop(J, 1);
}

/* ---------------- engine interface ---------------- */

struct javascript_context *js_create_context(void *p, long id)
{
	struct javascript_context *ctx;

	(void)p; (void)id;
	ctx = mem_alloc(sizeof(struct javascript_context));
	if (!ctx) return NULL;
	memset(ctx, 0, sizeof(struct javascript_context));
	ctx->ptr = p;
	ctx->id = id;
	ctx->js_id = ++mujs_context_counter;
	ctx->cookies = NULL;
	ctx->zaplatim = 0;

	ctx->J = js_newstate(NULL, NULL, 0); /* JS_STDEXT not in 1.3.6 header */
	if (!ctx->J) {
		mem_free(ctx);
		return NULL;
	}
	js_setcontext(ctx->J, ctx);
	register_globals(ctx->J);
	return ctx;
}

void js_destroy_context(struct javascript_context *ctx)
{
	if (!ctx) return;
	ctx->dead = 1;
	if (ctx->J) js_freestate(ctx->J);
	if (ctx->cookies) js_mem_free(ctx->cookies);
	mem_free(ctx);
}

void js_execute_code(struct javascript_context *ctx, unsigned char *code,
		     int len, void (*done)(void *))
{
	unsigned char *zcode;
	int rc;

	if (!ctx || !code || len < 0) {
		if (done) done(ctx ? ctx->ptr : NULL);
		return;
	}
	zcode = mem_alloc((size_t)len + 1);
	if (!zcode) {
		if (done) done(ctx->ptr);
		return;
	}
	memcpy(zcode, code, (size_t)len);
	zcode[len] = 0;

	rc = js_dostring(ctx->J, (const char *)zcode);
	(void)rc; /* MuJS reports errors on stderr; keep the page alive */

	mem_free(zcode);
	if (done) done(ctx->ptr);
}

/* ---------------- old async-protocol entry points (no-ops) -------------- */

void js_downcall_vezmi_true(void *context) { (void)context; }
void js_downcall_vezmi_false(void *context) { (void)context; }
void js_downcall_vezmi_null(void *context) { (void)context; }
void js_downcall_quiet_game_over(void *context) { (void)context; }
void js_spec_vykill_timer(void *context, int a) { (void)context; (void)a; }
void js_downcall_vezmi_string(void *context, unsigned char *string)
{
	(void)context;
	if (string) mem_free(string);
}

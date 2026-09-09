/*
 * quickjs_engine.c -- QuickJS (ES2020) engine for Links 2.30 on DJGPP.
 * Same interface contract as mujs_engine.c (jsint.c is unchanged):
 *   js_create_context / js_destroy_context / js_execute_code
 *   + old async-downcall protocol stubs (provided by jsint.c itself).
 * DOM natives call jsint.c's js_upcall_* directly or deferred via
 * install_timer, mirroring the MuJS engine.
 */
#include "quickjs_engine.h"
#include "struct.h"


static long qjs_context_counter = 0;

/* QuickJS interrupt handler: polled during script execution. Keeps
 * Watt-32's packet pump alive while long synchronous scripts run
 * (without this, ARP/TCP state starves on real hardware -> EHOSTUNREACH
 * and SSL errors after visiting JS-heavy sites), and aborts runaway
 * scripts after QJS_MAX_SCRIPT_MS. */
#define QJS_MAX_SCRIPT_MS 20000
static JSValue qjs_current_exception;  /* not used; keep simple */

/* Script watchdog: QuickJS polls this while executing. After too many
 * polls we abort the script - WITHOUT this a hostile/looping page
 * (hn.algolia.com) froze the whole machine (only power-cycle helped).
 * Pure counting: no tcp_tick() here (that corrupted watt32 state). */
/* Base 30s plus 20s per 100KB of script: hardware measurement showed
 * a 2.6MB webpack bundle needs >220s just to EVALUATE on DOS hardware
 * (the download alone took 8 minutes). Cap 900s (15 min); a true
 * infinite loop still dies at the cap. */
#define QJS_SCRIPT_TIME_LIMIT_MS 30000
uttime qjs_script_deadline; /* shared with wolfssl_links_glue fetch guard */

static uttime qjs_script_budget(int len)
{
	long extra = (long)len / 100000L;   /* per 100KB */
	if (extra > 43) extra = 43;         /* cap: 30s + 870s */
	return QJS_SCRIPT_TIME_LIMIT_MS + (uttime)extra * 20000;
}

static int qjs_interrupt_handler(JSRuntime *rt, void *opaque)
{
	(void)rt;
	(void)opaque;
	if (!qjs_script_deadline) {
		/* no script armed the budget yet (bootstrap eval, early
		 * timers): arm it now instead of aborting instantly */
		qjs_script_deadline = get_time() + QJS_SCRIPT_TIME_LIMIT_MS;
		return 0;
	}
	if (get_time() > qjs_script_deadline) {
		char mb[96];
		extern void sock_log2(const char *);
		snprintf(mb, sizeof mb,
			"QJS-INTERRUPT aborted runaway script (budget exceeded)");
		sock_log2(mb);
		return 1;  /* abort with InternalError */
	}
	return 0;
}

static void qjs_log(const char *msg)
{
	FILE *f = fopen("C:\\QJSLOG.TXT", "a");
	if (f) { fputs(msg, f); fputc('\n', f); fclose(f); }
}

/* engine-side globals formerly in the old pomocny.c */
char *js_temp_var_for_stracpy1;
size_t js_js_temp_var2;
void *js_js_temp_var1;
long js_zaflaknuto_pameti;

/* ---------------- DOM natives ---------------- */

static struct javascript_context *ctxof(JSContext *ctx)
{
	return (struct javascript_context *)JS_GetContextOpaque(ctx);
}

static JSValue qj_remove_element(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv)
{
	struct javascript_context *c = ctxof(ctx);
	const char *id = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	if (id && *id)
		js_upcall_document_remove_element(c->ptr, id);
	if (id) JS_FreeCString(ctx, id);
	return JS_UNDEFINED;
}

static JSValue qj_document_replace_page(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{
	struct javascript_context *c = ctxof(ctx);
	const char *s = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	if (s && *s)
		js_upcall_document_replace(c->ptr, (unsigned char *)s,
					   (int)strlen(s));
	if (s) JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

static JSValue qj_document_write(JSContext *ctx, JSValueConst this_val,
				 int argc, JSValueConst *argv)
{
	struct javascript_context *c = ctxof(ctx);
	int i;
	for (i = 0; i < argc; i++) {
		const char *s = JS_ToCString(ctx, argv[i]);
		if (s && *s)
			js_upcall_document_write(c->ptr, (unsigned char *)s,
						 (int)strlen(s));
		if (s) JS_FreeCString(ctx, s);
	}
	return JS_UNDEFINED;
}

static JSValue qj_document_writeLn(JSContext *ctx, JSValueConst this_val,
				   int argc, JSValueConst *argv)
{
	qj_document_write(ctx, this_val, argc, argv);
	js_upcall_document_write(ctxof(ctx)->ptr, (unsigned char *)"\n", 1);
	return JS_UNDEFINED;
}

static JSValue qj_get_cookie(JSContext *ctx, JSValueConst this_val,
			     int argc, JSValueConst *argv)
{
	struct javascript_context *c = ctxof(ctx);
	return JS_NewString(ctx, c && c->cookies ? (const char *)c->cookies : "");
}

static JSValue qj_set_cookie(JSContext *ctx, JSValueConst this_val,
			     int argc, JSValueConst *argv)
{
	struct javascript_context *c = ctxof(ctx);
	const char *cv;
	size_t la, lb;
	unsigned char *joined;
	if (argc < 1 || !c)
		return JS_UNDEFINED;
	cv = JS_ToCString(ctx, argv[0]);
	if (!cv || !*cv)
		return JS_UNDEFINED;
	if (c->cookies) {
		la = strlen((const char *)c->cookies);
		lb = strlen(cv);
		joined = (unsigned char *)js_mem_alloc(la + 2 + lb + 1);
		if (joined) {
			memcpy(joined, c->cookies, la);
			joined[la] = ';'; joined[la + 1] = ' ';
			memcpy(joined + la + 2, cv, lb);
			joined[la + 2 + lb] = 0;
			js_mem_free(c->cookies);
			c->cookies = joined;
		}
	} else {
		c->cookies = (unsigned char *)js_mem_alloc(strlen(cv) + 1);
		if (c->cookies) strcpy((char *)c->cookies, cv);
	}
	JS_FreeCString(ctx, cv);
	return JS_UNDEFINED;
}

static void defer_alert(struct javascript_context *c, const char *msg)
{
	struct fax_me_tender_string *fax = mem_alloc(sizeof(struct fax_me_tender_string));
	if (!fax) return;
	fax->ident = c->ptr;
	fax->string = stracpy((const unsigned char *)(msg ? msg : ""));
	install_timer(1, (void (*)(void *))js_upcall_alert, fax);
}

static JSValue qj_alert(JSContext *ctx, JSValueConst this_val,
			int argc, JSValueConst *argv)
{
	const char *s = argc > 0 ? JS_ToCString(ctx, argv[0]) : "";
	defer_alert(ctxof(ctx), s ? s : "");
	if (s) JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

static JSValue qj_location_set(JSContext *ctx, JSValueConst this_val,
			       int argc, JSValueConst *argv)
{
	struct javascript_context *c = ctxof(ctx);
	struct fax_me_tender_string *fax;
	const char *url = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	if (c && url && *url) {
		fax = mem_alloc(sizeof(struct fax_me_tender_string));
		if (fax) {
			fax->ident = c->ptr;
			fax->string = stracpy((const unsigned char *)url);
			js_upcall_goto_url(fax);
		}
	}
	if (url) JS_FreeCString(ctx, url);
	return JS_UNDEFINED;
}

static JSValue qj_location_get(JSContext *ctx, JSValueConst this_val,
			       int argc, JSValueConst *argv)
{
	unsigned char *url = js_upcall_get_location(ctxof(ctx)->ptr);
	JSValue v = JS_NewString(ctx, url ? (const char *)url : "");
	if (url) mem_free(url);
	return v;
}

/* Extract a component of the current URL: 'host', 'hostname', 'protocol',
 * 'origin', 'pathname'. Kept deliberately tiny: enough for scripts doing
 * location.host.includes("...") / location.protocol checks. */
static JSValue qj_location_part(JSContext *ctx, int part)
{
	unsigned char *url = js_upcall_get_location(ctxof(ctx)->ptr);
	const char *s = url ? (const char *)url : "";
	const char *p = strstr(s, "://");
	const char *host = p ? p + 3 : s;
	const char *slash = strchr(host, '/');
	char buf[256];
	size_t hl = slash ? (size_t)(slash - host) : strlen(host);
	if (hl > sizeof(buf) - 1) hl = sizeof(buf) - 1;
	if (url) mem_free(url);
	switch (part) {
	case 0: /* host or hostname (no port split for now) */
		memcpy(buf, host, hl); buf[hl] = 0; return JS_NewString(ctx, buf);
	case 1: { /* protocol */
		const char *c = strchr(s, ':');
		size_t l = c ? (size_t)(c - s) : 0;
		if (l > 15) l = 15;
		memcpy(buf, s, l); buf[l] = 0;
		strcat(buf, ":");
		return JS_NewString(ctx, buf);
	}
	case 2: { /* origin */
		int plen = p ? (int)(p - s) + 3 : 0;
		snprintf(buf, sizeof(buf), "%.*s%s", plen, s, host);
		/* buf currently url up to path start */
		return JS_NewStringLen(ctx, s, plen + hl);
	}
	default:
		return JS_NewString(ctx, "");
	}
}

static JSValue qj_location_host(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{	return qj_location_part(ctx, 0);	}
static JSValue qj_location_hostname(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{	return qj_location_part(ctx, 0);	}
static JSValue qj_location_protocol(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{	return qj_location_part(ctx, 1);	}
static JSValue qj_location_origin(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{	return qj_location_part(ctx, 2);	}
static JSValue qj_location_pathname(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	unsigned char *url = js_upcall_get_location(ctxof(ctx)->ptr);
	const char *s = url ? (const char *)url : "";
	const char *p = strstr(s, "://");
	const char *host = p ? p + 3 : s;
	const char *slash = strchr(host, '/');
	JSValue r = JS_NewString(ctx, slash ? slash : "/");
	if (url) mem_free(url);
	return r;
}

#define QJ_GET_STR(fname, upcall) \
static JSValue fname(JSContext *ctx, JSValueConst t, int a, JSValueConst *v) \
{ \
	unsigned char *s = upcall(ctxof(ctx)->ptr); \
	JSValue r = JS_NewString(ctx, s ? (const char *)s : ""); \
	if (s) mem_free(s); \
	return r; \
}
QJ_GET_STR(qj_get_title, js_upcall_get_title)
QJ_GET_STR(qj_get_referrer, js_upcall_get_referrer)

static JSValue qj_get_appname(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	unsigned char *s = js_upcall_get_appname();
	JSValue r = JS_NewString(ctx, s ? (const char *)s : "");
	if (s) mem_free(s);
	return r;
}

static JSValue qj_get_useragent(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	unsigned char *s = js_upcall_get_useragent(ctxof(ctx)->ptr);
	JSValue r = JS_NewString(ctx, s ? (const char *)s : "");
	if (s) mem_free(s);
	return r;
}



/* property-access tracer for challenge scripts (Cloudflare etc.):
 * JS-side Proxies log each first access of a property name here. */
static JSValue qj_trace_log(JSContext *ctx, JSValueConst this_val,
			    int argc, JSValueConst *argv)
{
	const char *s = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	FILE *f;
	if (!s || !*s) { if (s) JS_FreeCString(ctx, s); return JS_UNDEFINED; }
	f = fopen("C:\\JSTRACE.LOG", "a");
	if (f) { fputs(s, f); fputc('\n', f); fclose(f); }
	JS_FreeCString(ctx, s);
	return JS_UNDEFINED;
}

/* native HTTP for the bootstrap XHR: returns {status, body} or null */
static JSValue qj_http_native(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
	const char *url = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	const char *method = argc > 1 ? JS_ToCString(ctx, argv[1]) : NULL;
	const char *ctype = argc > 2 ? JS_ToCString(ctx, argv[2]) : NULL;
	const char *body = argc > 3 ? JS_ToCString(ctx, argv[3]) : NULL;
	char *resp = NULL;
	long len = 0;
	int status = 0;
	JSValue o = JS_NULL;
	unsigned char *uau;
	char ua[128];

	if (!url || !*url) goto ret;
	uau = js_upcall_get_useragent(ctxof(ctx)->ptr);
	snprintf(ua, sizeof ua, "%s", uau ? (const char *)uau : "Links");
	if (uau) mem_free(uau);
	if (qjs_http_request(url, method && *method ? method : "GET",
			     ctype && *ctype ? ctype : NULL,
			     body && *body ? body : NULL, ua,
			     &resp, &len, &status) == 0) {
		char *hdr_end = NULL;
		long i;
		for (i = 0; i + 3 < len; i++)
			if (resp[i] == '\r' && resp[i+1] == '\n' &&
			    resp[i+2] == '\r' && resp[i+3] == '\n') {
				hdr_end = resp + i;
				break;
			}
		o = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, status));
		if (hdr_end)
			JS_SetPropertyStr(ctx, o, "body",
				JS_NewStringLen(ctx, hdr_end + 4, len - (hdr_end + 4 - resp)));
		else
			JS_SetPropertyStr(ctx, o, "body", JS_NewStringLen(ctx, resp, len));
	}
ret:
	free(resp);
	JS_FreeCString(ctx, url);
	JS_FreeCString(ctx, method);
	JS_FreeCString(ctx, ctype);
	JS_FreeCString(ctx, body);
	return o;
}

/* ---------------- real setTimeout via Links install_timer ---------------- */
struct qjs_timer {
	struct javascript_context *c;
	struct timer *tm;
	JSValue func;
	JSValue arg;
};

static void qjs_pump_jobs(struct javascript_context *c)
{
	for (;;) {
		JSContext *pc = c->ctx;
		int jr = JS_ExecutePendingJob(c->rt, &pc);
		if (jr == 0) break;
		if (jr < 0) {
			if (c->logged_error < 5) {
				c->logged_error++;
				JSValue ex = JS_GetException(c->ctx);
				const char *exs = JS_ToCString(c->ctx, ex);
				FILE *f = fopen("C:\\JSERROR.LOG", "a");
				if (f) { fprintf(f, "JOB: %s\n", exs ? exs : "?"); fclose(f); }
				if (exs) JS_FreeCString(c->ctx, exs);
				JS_FreeValue(c->ctx, ex);
			} else
				JS_FreeValue(c->ctx, JS_GetException(c->ctx));
			break;
		}
	}
}

static void qjs_timer_fire(struct qjs_timer *q)
{
	if (q->c && !q->c->dead && q->c->ctx) {
		JSValue r;
		qjs_script_deadline = get_time() + qjs_script_budget(100000);
		r = JS_Call(q->c->ctx, q->func, JS_UNDEFINED, 1, &q->arg);
		if (JS_IsException(r)) {
			JSValue ex = JS_GetException(q->c->ctx);
			if (q->c->logged_error < 5) {
				q->c->logged_error++;
				{
					const char *exs = JS_ToCString(q->c->ctx, ex);
					FILE *f = fopen("C:\\JSERROR.LOG", "a");
					if (f) { fprintf(f, "TIMER: %s\n", exs ? exs : "?"); fclose(f); }
					if (exs) JS_FreeCString(q->c->ctx, exs);
				}
			}
			JS_FreeValue(q->c->ctx, ex);
		} else
			JS_FreeValue(q->c->ctx, r);
		qjs_pump_jobs(q->c);
		JS_FreeValue(q->c->ctx, q->func);
		JS_FreeValue(q->c->ctx, q->arg);
	}
	mem_free(q);
}

static int qjs_timer_ids;

static JSValue qj_set_timeout(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
	struct javascript_context *c = ctxof(ctx);
	double ms = 1;
	struct qjs_timer *q;
	if (!c || argc < 1 || !JS_IsFunction(ctx, argv[0]))
		return JS_NewInt32(ctx, 0);
	if (argc > 1 && JS_IsNumber(argv[1])) {
		if (JS_ToFloat64(ctx, &ms, argv[1]) < 0) ms = 1;
		if (ms < 1) ms = 1;
		if (ms > 600000) ms = 600000;
	}
	q = mem_alloc(sizeof(struct qjs_timer));
	if (!q) return JS_NewInt32(ctx, 0);
	q->c = c;
	q->func = JS_DupValue(ctx, argv[0]);
	q->arg = argc > 2 ? JS_DupValue(ctx, argv[2]) : JS_UNDEFINED;
	q->tm = install_timer((uttime)ms, (void (*)(void *))qjs_timer_fire, q);
	return JS_NewInt32(ctx, ++qjs_timer_ids);
}

static JSValue qj_clear_timeout(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
	/* we do not track id -> timer: clear is a no-op (matches the old
	 * behavior; timers are short-lived one-shots) */
	return JS_UNDEFINED;
}

/* ---------------- fetch(): synchronous blocking HTTP(S) ---------------- */
extern int qjs_http_request(const char *url, const char *method,
	const char *content_type, const char *body, const char *user_agent,
	char **out, long *outlen, int *status);

static JSValue qj_resp_text(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	/* body string stored on `this` as __body at construction */
	JSValue b = JS_GetPropertyStr(ctx, t, "__body");
	return b;
}

static JSValue qj_resp_json(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	JSValue b = JS_GetPropertyStr(ctx, t, "__body");
	const char *s = JS_ToCString(ctx, b);
	JSValue r = JS_ParseJSON(ctx, s, strlen(s), "<response>");
	JS_FreeCString(ctx, s);
	JS_FreeValue(ctx, b);
	return r;
}

static char *qjs_resolve_url(JSContext *ctx, const char *url)
{
	/* absolute? */
	if (!strncasecmp(url, "http://", 7) || !strncasecmp(url, "https://", 8))
		return strdup(url);
	{
		unsigned char *locu = js_upcall_get_location(ctxof(ctx)->ptr);
		const char *loc = locu ? (const char *)locu : "";
		char *base = strdup(loc);
		char *out;
		const char *scheme = strncasecmp(loc, "https:", 6) ? "http" : "https";
		char *host = strstr(base, "://");
		if (url[0] == '/' && url[1] == '/') {
			/* //host/path -> scheme://host/path */
			out = malloc(strlen(scheme) + strlen(url) + 2);
			if (out) sprintf(out, "%s:%s", scheme, url);
		} else if (url[0] == '/') {
			char *path;
			if (!host) { free(base); if (locu) mem_free(locu); return strdup(url); }
			host += 3;
			path = strchr(host, '/');
			if (path) *path = 0;
			out = malloc(strlen(scheme) + strlen(host) + strlen(url) + 8);
			if (out) sprintf(out, "%s://%s%s", scheme, host, url);
		} else {
			/* relative: strip last path segment of base */
			char *slash;
			if (!host) { free(base); if (locu) mem_free(locu); return strdup(url); }
			host += 3;
			slash = strrchr(host, '/');
			if (slash) slash[1] = 0; else strcat(base, "/");
			out = malloc(strlen(base) + strlen(url) + 2);
			if (out) sprintf(out, "%s%s", base, url);
		}
		free(base);
		if (locu) mem_free(locu);
		return out;
	}
}

static JSValue qj_fetch(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
{
	const char *url = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
	const char *method = "GET";
	char *method_dup = NULL, *ctype_dup = NULL, *body_dup = NULL;
	const char *ctype = NULL, *body = NULL;
	char *absurl, *resp = NULL;
	long resplen = 0;
	int status = 0;
	JSValue result = JS_UNDEFINED;
	JSValue funcs[2];
	JSValue promise, resolve_args[1];
	unsigned char *uau;
	char ua[128];
	JSValue resp_obj, body_val;
	char *hdr_end;

	if (!url) return JS_EXCEPTION;
	if (argc > 1 && JS_IsObject(argv[1])) {
		JSValue m = JS_GetPropertyStr(ctx, argv[1], "method");
		const char *ms = JS_ToCString(ctx, m);
		if (ms && *ms) { method_dup = strdup(ms); if (method_dup) method = method_dup; }
		JS_FreeCString(ctx, ms);
		JS_FreeValue(ctx, m);
		{
			JSValue h = JS_GetPropertyStr(ctx, argv[1], "headers");
			if (JS_IsObject(h)) {
				JSValue ct = JS_GetPropertyStr(ctx, h, "Content-Type");
				if (!JS_IsUndefined(ct) && !JS_IsException(ct))
						ctype = strdup(JS_ToCString(ctx, ct) ? : NULL);
				JS_FreeValue(ctx, ct);
			}
			JS_FreeValue(ctx, h);
		}
		{
			JSValue b = JS_GetPropertyStr(ctx, argv[1], "body");
			if (JS_IsString(b))
				body = strdup(JS_ToCString(ctx, b));
			else if (!JS_IsUndefined(b) && !JS_IsNull(b)) {
				/* JSON-serialise non-string bodies */
				JSValue js = JS_JSONStringify(ctx, b, JS_UNDEFINED, JS_UNDEFINED);
				if (JS_IsString(js)) {
					const char *js_s = JS_ToCString(ctx, js);
					if (js_s) { body_dup = strdup(js_s); if (body_dup) body = body_dup; }
				}
				JS_FreeValue(ctx, js);
			}
			JS_FreeValue(ctx, b);
		}
		if (body && !ctype) ctype = "application/json";
	}

	absurl = qjs_resolve_url(ctx, url);
	uau = js_upcall_get_useragent(ctxof(ctx)->ptr);
	snprintf(ua, sizeof ua, "%s", uau ? (const char *)uau : "Links");
	if (uau) mem_free(uau);

	if (qjs_http_request(absurl ? absurl : url, method, ctype, body, ua,
			     &resp, &resplen, &status) != 0) {
		char m[256];
		snprintf(m, sizeof m, "FETCHFAIL url=%.120s", absurl ? absurl : url);
		{ extern void sock_log2(const char *); sock_log2(m); }
		goto ret;
	}

	/* split off headers: body begins after first \r\n\r\n */
	hdr_end = NULL;
	{
		long i;
		for (i = 0; i + 3 < resplen; i++)
			if (resp[i] == '\r' && resp[i+1] == '\n' && resp[i+2] == '\r' && resp[i+3] == '\n') {
				hdr_end = resp + i;
				break;
			}
	}
	if (hdr_end) {
		body_val = JS_NewStringLen(ctx, hdr_end + 4, resplen - (hdr_end + 4 - resp));
	} else {
		/* no header split: whole payload is the body (HTTP/1.0-style) */
		body_val = JS_NewStringLen(ctx, resp, resplen);
		status = status ? status : 200;
	}

	resp_obj = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, resp_obj, "__body", body_val);
	JS_SetPropertyStr(ctx, resp_obj, "ok", JS_NewBool(ctx, status >= 200 && status < 300));
	JS_SetPropertyStr(ctx, resp_obj, "status", JS_NewInt32(ctx, status));
	JS_SetPropertyStr(ctx, resp_obj, "statusText", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, resp_obj, "url", JS_NewString(ctx, absurl ? absurl : url));
	JS_SetPropertyStr(ctx, resp_obj, "text", JS_NewCFunction(ctx, qj_resp_text, "text", 0));
	JS_SetPropertyStr(ctx, resp_obj, "json", JS_NewCFunction(ctx, qj_resp_json, "json", 0));

	/* resolve a promise with the response */
	promise = JS_NewPromiseCapability(ctx, funcs);
	if (JS_IsException(promise)) {
		JS_FreeValue(ctx, resp_obj);
		goto ret;
	}
	resolve_args[0] = resp_obj;
	JS_FreeValue(ctx, JS_Call(ctx, funcs[0], JS_UNDEFINED, 1, resolve_args));
	JS_FreeValue(ctx, funcs[0]);
	JS_FreeValue(ctx, funcs[1]);
	result = promise;
ret:
	free(resp);
	free(absurl);
	free(method_dup);
	free(ctype_dup);
	free(body_dup);
	JS_FreeCString(ctx, url);
	return result;
}

static JSValue qj_noop(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	return JS_UNDEFINED;
}

static JSValue qj_create_element(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	/* fake element: id, style {}, className, appendChild no-op chain */
	JSValue el = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, el, "style", JS_NewObject(ctx));
	JS_SetPropertyStr(ctx, el, "className", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, el, "innerHTML", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, el, "appendChild",
		JS_NewCFunction(ctx, qj_noop, "appendChild", 1));
	JS_SetPropertyStr(ctx, el, "setAttribute",
		JS_NewCFunction(ctx, qj_noop, "setAttribute", 2));
	JS_SetPropertyStr(ctx, el, "addEventListener",
		JS_NewCFunction(ctx, qj_noop, "addEventListener", 3));
	return el;
}


static JSValue qj_query_selector(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	/* no real DOM tree: never match */
	return JS_NULL;
}

static JSValue qj_query_selector_all(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	JSValue list = JS_NewArray(ctx);
	/* empty NodeList: length 0 — forEach/map-safe enough for feature probes */
	return list;
}

static JSValue qj_get_elements_by_tag_name(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	return JS_NewArray(ctx);
}

static JSValue qj_encodeURIComponent(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	const char *in = a > 0 ? JS_ToCString(ctx, v[0]) : "";
	const char *hex = "0123456789ABCDEF";
	JSValue out;
	char *buf, *o;
	if (!in) return JS_UNDEFINED;
	buf = malloc(strlen(in) * 3 + 1);
	if (!buf) { if (in) JS_FreeCString(ctx, in); return JS_UNDEFINED; }
	o = buf;
	for (; *in; in++) {
		unsigned char c = (unsigned char)*in;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || strchr("-_.!~*'()", c))
			*o++ = c;
		else {
			*o++ = '%'; *o++ = hex[c >> 4]; *o++ = hex[c & 15];
		}
	}
	*o = 0;
	out = JS_NewString(ctx, buf);
	free(buf);
	return out;
}

static JSValue qj_decodeURIComponent(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	const char *in = a > 0 ? JS_ToCString(ctx, v[0]) : "";
	char *buf, *o;
	JSValue out;
	if (!in) return JS_UNDEFINED;
	buf = malloc(strlen(in) + 1);
	if (!buf) { JS_FreeCString(ctx, in); return JS_UNDEFINED; }
	o = buf;
	while (*in) {
		if (*in == '%' && in[1] && in[2]) {
			char h[3] = { in[1], in[2], 0 };
			*o++ = (char)strtol(h, NULL, 16);
			in += 3;
		} else
			*o++ = *in++;
	}
	*o = 0;
	out = JS_NewString(ctx, buf);
	free(buf);
	return out;
}

static JSValue qj_remove_child(JSContext *ctx, JSValueConst t, int a, JSValueConst *v);

static JSValue qj_get_element_by_id(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	/* Minimal node: only what the anti-clickjacking snippet touches */
	JSValue node;
	const char *id = a > 0 ? JS_ToCString(ctx, v[0]) : NULL;
	if (!id) return JS_NULL;
	node = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, node, "id", JS_NewString(ctx, id));
	{
		JSValue pn = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, pn, "removeChild",
			JS_NewCFunction(ctx, qj_remove_child, "removeChild", 1));
		JS_SetPropertyStr(ctx, node, "parentNode", pn);
	}
	return node;
}

static JSValue qj_remove_child(JSContext *ctx, JSValueConst t, int a, JSValueConst *v)
{
	/* return the removed child per DOM spec */
	return a > 0 ? JS_DupValue(ctx, v[0]) : JS_NULL;
}

static void register_globals(JSContext *ctx)
{
	JSValue doc, win, forms, proto;

	/* document */
	doc = JS_NewObject(ctx);
	proto = JS_GetPrototype(ctx, doc);
	JS_SetPropertyStr(ctx, doc, "write", JS_NewCFunction(ctx, qj_document_write, "write", 0));
	JS_SetPropertyStr(ctx, doc, "writeln", JS_NewCFunction(ctx, qj_document_writeLn, "writeln", 0));
	JS_SetPropertyStr(ctx, doc, "__qjsReplacePage",
		JS_NewCFunction(ctx, qj_document_replace_page, "__qjsReplacePage", 1));
	JS_DefinePropertyGetSet(ctx, doc, JS_NewAtom(ctx, "cookie"),
		JS_NewCFunction(ctx, qj_get_cookie, "getCookie", 0),
		JS_NewCFunction(ctx, qj_set_cookie, "setCookie", 1),
		JS_PROP_C_W_E);
	JS_DefinePropertyGetSet(ctx, doc, JS_NewAtom(ctx, "title"),
		JS_NewCFunction(ctx, qj_get_title, "getTitle", 0),
		JS_NewCFunction(ctx, qj_noop, "setTitle", 0),
		JS_PROP_C_W_E);
	JS_DefinePropertyGetSet(ctx, doc, JS_NewAtom(ctx, "referrer"),
		JS_NewCFunction(ctx, qj_get_referrer, "getReferrer", 0),
		JS_NewCFunction(ctx, qj_noop, "setReferrer", 0),
		JS_PROP_C_W_E);
	forms = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, forms, "length", JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, doc, "forms", forms);
	{
		JSValue proto_node = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, proto_node, "removeChild",
			JS_NewCFunction(ctx, qj_remove_child, "removeChild", 1));
		/* make getElementById results' parentNode have removeChild */
		/* simple: attach removeChild to document too */
		JS_SetPropertyStr(ctx, doc, "getElementById",
			JS_NewCFunction(ctx, qj_get_element_by_id, "getElementById", 1));
		JS_SetPropertyStr(ctx, doc, "createElement",
			JS_NewCFunction(ctx, qj_create_element, "createElement", 1));
		JS_SetPropertyStr(ctx, doc, "removeChild",
			JS_NewCFunction(ctx, qj_remove_child, "removeChild", 1));
		JS_SetPropertyStr(ctx, doc, "querySelector",
			JS_NewCFunction(ctx, qj_query_selector, "querySelector", 1));
		JS_SetPropertyStr(ctx, doc, "querySelectorAll",
			JS_NewCFunction(ctx, qj_query_selector_all, "querySelectorAll", 1));
		JS_SetPropertyStr(ctx, doc, "getElementsByTagName",
			JS_NewCFunction(ctx, qj_get_elements_by_tag_name, "getElementsByTagName", 1));
		JS_SetPropertyStr(ctx, doc, "addEventListener",
			JS_NewCFunction(ctx, qj_noop, "addEventListener", 3));
		JS_SetPropertyStr(ctx, doc, "body", qj_create_element(ctx, JS_UNDEFINED, 0, NULL));
		JS_SetPropertyStr(ctx, doc, "head", qj_create_element(ctx, JS_UNDEFINED, 0, NULL));
		JS_FreeValue(ctx, proto_node);
	}
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "document", doc);

	/* window: MUST be the global object itself (window === globalThis,
	 * like a real browser) — jQuery/UMD bundles export onto window and
	 * later scripts read them as bare identifiers via global scope */
	win = JS_GetGlobalObject(ctx);
	JS_SetPropertyStr(ctx, win, "alert", JS_NewCFunction(ctx, qj_alert, "alert", 1));
	/* NOTE: do NOT re-set "document" on win — win IS the global object
	 * and a duplicate C-level JS_SetPropertyStr of the same key frees
	 * live heap (QuickJS bug verified with ASAN) */
	{
		/* location must be an OBJECT: jQuery/bld_next do window.location.host
		 * / .href.indexOf(...) — a plain string accessor broke them */
		JSValue loc = JS_NewObject(ctx);
		JS_DefinePropertyGetSet(ctx, loc, JS_NewAtom(ctx, "href"),
			JS_NewCFunction(ctx, qj_location_get, "getHref", 0),
			JS_NewCFunction(ctx, qj_location_set, "setHref", 1),
			JS_PROP_C_W_E);
		JS_DefinePropertyGetSet(ctx, loc, JS_NewAtom(ctx, "host"),
			JS_NewCFunction(ctx, qj_location_host, "getHost", 0),
			JS_NewCFunction(ctx, qj_noop, "setHost", 1), JS_PROP_C_W_E);
		JS_DefinePropertyGetSet(ctx, loc, JS_NewAtom(ctx, "hostname"),
			JS_NewCFunction(ctx, qj_location_hostname, "getHostname", 0),
			JS_NewCFunction(ctx, qj_noop, "setHostname", 1), JS_PROP_C_W_E);
		JS_DefinePropertyGetSet(ctx, loc, JS_NewAtom(ctx, "protocol"),
			JS_NewCFunction(ctx, qj_location_protocol, "getProtocol", 0),
			JS_NewCFunction(ctx, qj_noop, "setProtocol", 1), JS_PROP_C_W_E);
		JS_DefinePropertyGetSet(ctx, loc, JS_NewAtom(ctx, "origin"),
			JS_NewCFunction(ctx, qj_location_origin, "getOrigin", 0),
			JS_NewCFunction(ctx, qj_noop, "setOrigin", 1), JS_PROP_C_W_E);
		JS_DefinePropertyGetSet(ctx, loc, JS_NewAtom(ctx, "pathname"),
			JS_NewCFunction(ctx, qj_location_pathname, "getPathname", 0),
			JS_NewCFunction(ctx, qj_noop, "setPathname", 1), JS_PROP_C_W_E);
		JS_SetPropertyStr(ctx, loc, "toString",
			JS_NewCFunction(ctx, qj_location_get, "toString", 0));
		JS_SetPropertyStr(ctx, win, "location", loc);
		/* global location alias too */
		JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "location",
			JS_GetPropertyStr(ctx, win, "location"));
	}
	JS_DefinePropertyGetSet(ctx, win, JS_NewAtom(ctx, "appName"),
		JS_NewCFunction(ctx, qj_get_appname, "getAppName", 0),
		JS_NewCFunction(ctx, qj_noop, "setAppName", 0),
		JS_PROP_C_W_E);
	JS_DefinePropertyGetSet(ctx, win, JS_NewAtom(ctx, "userAgent"),
		JS_NewCFunction(ctx, qj_get_useragent, "getUserAgent", 0),
		JS_NewCFunction(ctx, qj_noop, "setUserAgent", 0),
		JS_PROP_C_W_E);
	JS_SetPropertyFunctionList(ctx, win, (JSCFunctionListEntry[]){
		JS_CFUNC_DEF("setStatus", 0, qj_noop),
	}, 1);
	/* win IS the global object: bind the name window to itself */
	JS_SetPropertyStr(ctx, win, "window", win);
	JS_SetPropertyStr(ctx, win, "fetch", JS_NewCFunction(ctx, qj_fetch, "fetch", 2));
	JS_SetPropertyStr(ctx, win, "setTimeout", JS_NewCFunction(ctx, qj_set_timeout, "setTimeout", 2));
	JS_SetPropertyStr(ctx, win, "clearTimeout", JS_NewCFunction(ctx, qj_clear_timeout, "clearTimeout", 1));
	JS_SetPropertyStr(ctx, win, "__linksHttp", JS_NewCFunction(ctx, qj_http_native, "__linksHttp", 4));
	JS_SetPropertyStr(ctx, win, "__linksRemoveElement", JS_NewCFunction(ctx, qj_remove_element, "__linksRemoveElement", 1));
	JS_SetPropertyStr(ctx, win, "__qjsTraceLog", JS_NewCFunction(ctx, qj_trace_log, "__qjsTraceLog", 1));

	/* navigator: sites probe navigator.userAgent / .platform / .language */
	{
		JSValue nav = JS_NewObject(ctx);
		JS_DefinePropertyGetSet(ctx, nav, JS_NewAtom(ctx, "userAgent"),
			JS_NewCFunction(ctx, qj_get_useragent, "getUserAgent", 0),
			JS_NewCFunction(ctx, qj_noop, "setUserAgent", 0),
			JS_PROP_C_W_E);
		JS_DefinePropertyGetSet(ctx, nav, JS_NewAtom(ctx, "appName"),
			JS_NewCFunction(ctx, qj_get_appname, "getAppName", 0),
			JS_NewCFunction(ctx, qj_noop, "setAppName", 0),
			JS_PROP_C_W_E);
		JS_SetPropertyStr(ctx, nav, "appVersion", JS_NewString(ctx, "5.0 (DOS)"));
		JS_SetPropertyStr(ctx, nav, "platform", JS_NewString(ctx, "DOS"));
		JS_SetPropertyStr(ctx, nav, "language", JS_NewString(ctx, "nl"));
		JS_SetPropertyStr(ctx, nav, "cookieEnabled", JS_NewBool(ctx, 1));
		JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "navigator", nav);
	}

	/* addEventListener/removeEventListener stubs: sites call them at
	 * load time; a silent no-op keeps page scripts running */
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "addEventListener",
		JS_NewCFunction(ctx, qj_noop, "addEventListener", 3));
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "removeEventListener",
		JS_NewCFunction(ctx, qj_noop, "removeEventListener", 3));
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "encodeURIComponent",
		JS_NewCFunction(ctx, qj_encodeURIComponent, "encodeURIComponent", 1));
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "decodeURIComponent",
		JS_NewCFunction(ctx, qj_decodeURIComponent, "decodeURIComponent", 1));

	/* global alert alias */
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "alert",
		JS_NewCFunction(ctx, qj_alert, "alert", 1));
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "self", JS_GetGlobalObject(ctx));
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "top", JS_GetGlobalObject(ctx));

	/* localStorage / sessionStorage: no-op Storage objects (feature probes,
	 * jQuery-era try/catch getItem/setItem) */
	{
		JSValue store = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, store, "getItem", JS_NewCFunction(ctx, qj_noop, "getItem", 1));
		JS_SetPropertyStr(ctx, store, "setItem", JS_NewCFunction(ctx, qj_noop, "setItem", 2));
		JS_SetPropertyStr(ctx, store, "removeItem", JS_NewCFunction(ctx, qj_noop, "removeItem", 1));
		JS_SetPropertyStr(ctx, store, "clear", JS_NewCFunction(ctx, qj_noop, "clear", 0));
		JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "localStorage", store);
		JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "sessionStorage", store);
	}

	/* HTMLElement: webcomponents.js (customElements) probes it; a plain
	 * function object satisfies typeof === 'function' and subclass probes */
	/* HTMLElement defined in dom_bootstrap.js as a JS function: C
	 * functions are not valid ES class bases in QuickJS */

	/* Intl: belastingdienst.js / bld-webcomponents.js throw without it.
	 * Minimal stubs: constructors returning do-nothing instances. */
	{
		JSValue intl = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, intl, "Collator",
			JS_NewCFunction(ctx, qj_noop, "Collator", 2));
		JS_SetPropertyStr(ctx, intl, "DateTimeFormat",
			JS_NewCFunction(ctx, qj_noop, "DateTimeFormat", 2));
		JS_SetPropertyStr(ctx, intl, "NumberFormat",
			JS_NewCFunction(ctx, qj_noop, "NumberFormat", 2));
		JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "Intl", intl);
	}

	JS_FreeValue(ctx, proto);
}

/* Called from view.c get_form_url(): hand the encoded form data to the
 * page's JS, dispatch a synthetic submit event, run the fallback search.
 * Returns nonzero if JS called preventDefault (native submit cancelled). */
int qjs_form_submit(struct javascript_context *c, const char *formdata)
{
	JSValue g, v, r;
	int prevented = 0;
	if (!c || !c->ctx) return 0;
	qjs_script_deadline = get_time() + QJS_SCRIPT_TIME_LIMIT_MS;
	g = JS_GetGlobalObject(c->ctx);
	JS_SetPropertyStr(c->ctx, g, "__qjsFormRaw",
		JS_NewString(c->ctx, formdata ? formdata : ""));
	JS_FreeValue(c->ctx, g);
	r = JS_Eval(c->ctx, "__qjsOnFormSubmit()", 18, "<formsubmit>",
		JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r))
		JS_FreeValue(c->ctx, JS_GetException(c->ctx));
	else
		JS_FreeValue(c->ctx, r);
	qjs_pump_jobs(c);
	g = JS_GetGlobalObject(c->ctx);
	v = JS_GetPropertyStr(c->ctx, g, "__qjsPreventDefault");
	if (JS_IsBool(v)) prevented = !!JS_ToBool(c->ctx, v);
	JS_FreeValue(c->ctx, v);
	JS_FreeValue(c->ctx, g);
	return prevented;
}

/* Dispatch a DOM event (e.g. 'click') to runtime-registered JS
 * listeners. Returns nonzero if a listener called preventDefault. */
int qjs_dispatch_event(struct javascript_context *c, const char *type)
{
	JSValue g, v, r;
	char code[64];
	int prevented = 0;
	if (!c || !c->ctx) return 0;
	qjs_script_deadline = get_time() + QJS_SCRIPT_TIME_LIMIT_MS;
	{
		char mb[96];
		extern void sock_log2(const char *);
		snprintf(mb, sizeof mb, "DISPATCH type=%s", type ? type : "?");
		sock_log2(mb);
	}
	snprintf(code, sizeof code, "__qjsDispatch(\"%s\")",
		 type && *type ? type : "click");
	r = JS_Eval(c->ctx, code, strlen(code), "<dispatch>",
		JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r))
		JS_FreeValue(c->ctx, JS_GetException(c->ctx));
	else
		JS_FreeValue(c->ctx, r);
	qjs_pump_jobs(c);
	g = JS_GetGlobalObject(c->ctx);
	v = JS_GetPropertyStr(c->ctx, g, "__qjsPreventDefault");
	if (JS_IsBool(v)) prevented = !!JS_ToBool(c->ctx, v);
	JS_FreeValue(c->ctx, v);
	JS_FreeValue(c->ctx, g);
	return prevented;
}

static const char qjs_dom_bootstrap[] =
	"/* dom_bootstrap.js — evaluated in every QuickJS context after the C\n"
	" * register_globals(). Enriches the minimal C stubs into a \"permissive\n"
	" * document\" good enough for jQuery 3.6 + Bootstrap site bundles.\n"
	" * Verified against www.belastingdienst.nl's script chain (host QuickJS\n"
	" * harness /tmp/guest_prelude5.js). Keep everything no-op/absorbing. */\n"
	"(function () {\n"
	"	\"use strict\";\n"
	"\n"
	"	function elem() {\n"
	"		return {\n"
	"			nodeType: 1, lang: \"nl\", style: {}, className: \"\", innerHTML: \"\", textContent: \"\", value: \"\",\n"
	"			checked: false,\n"
	"			parentNode: { removeChild: function () {} },\n"
	"			childNodes: [],\n"
	"			appendChild: function (c) { this.childNodes = [c]; return c; },\n"
	"			removeChild: function () {},\n"
	"			get lastChild() { return this.childNodes[this.childNodes.length - 1] || elem(); },\n"
	"			cloneNode: function () { var n = elem(); n.checked = this.checked; return n; },\n"
	"			setAttribute: function () {}, getAttribute: function () { return null; },\n"
	"			removeAttribute: function () {},\n"
	"			addEventListener: function (t, fn) {\n"
	"				if (typeof fn === \"function\" && globalThis.__qjsAddEventListener)\n"
	"					globalThis.__qjsAddEventListener(t, fn);\n"
	"			},\n"
	"			removeEventListener: function () {},\n"
	"			classList: {\n"
	"				add: function () {}, remove: function () {}, toggle: function () {},\n"
	"				contains: function () { return false; }\n"
	"			},\n"
	"			querySelector: function () { return elem(); },\n"
	"			querySelectorAll: function () { return []; },\n"
	"			getElementsByTagName: function () { return []; },\n"
	"			getElementsByClassName: function () { return []; },\n"
	"			contains: function () { return false; },\n"
	"			focus: function () {}, blur: function () {},\n"
	"			getBoundingClientRect: function () {\n"
	"				return { top: 0, left: 0, right: 0, bottom: 0,\n"
	"					width: 0, height: 0, x: 0, y: 0 };\n"
	"			},\n"
	"			matches: function () { return false; },\n"
	"			closest: function () { return null; },\n"
	"			getComputedStyle: function () { return {}; },\n"
	"			scrollIntoView: function () {},\n"
	"			insertAdjacentHTML: function () {},\n"
	"			replaceChildren: function () {},\n"
	"			/* canvas stub: belastingdienst.js noHTML5() probes\n"
	"			 * createElement(\"canvas\").getContext - without it the\n"
	"			 * site keeps the 'Javascript staat uit' block */\n"
	"			getContext: function () {\n"
	"				return {\n"
	"					fillRect: function () {}, clearRect: function () {},\n"
	"					getImageData: function () { return { data: [] }; },\n"
	"					putImageData: function () {}, drawImage: function () {},\n"
	"					measureText: function () { return { width: 0 }; },\n"
	"					beginPath: function () {}, arc: function () {},\n"
	"					fill: function () {}, stroke: function () {},\n"
	"					save: function () {}, restore: function () {},\n"
	"					translate: function () {}, scale: function () {}\n"
	"				};\n"
	"			},\n"
	"			toDataURL: function () { return \"data:,\"; }\n"
	"		};\n"
	"	}\n"
	"	globalThis.__qjs_elem = elem;\n"
	"\n"
	"	var __qjsTrackedIds = {};\n"
	"	globalThis.__qjsTrackedNodes = [];\n"
	"	function trackedElem(id) {\n"
	"		var e = globalThis.__qjsDomNode ? new globalThis.__qjsDomNode(\"div\") : elem();\n"
	"		e.__id = id || null;\n"
	"		var kill = function () {\n"
	"			if (e.__id && globalThis.__linksRemoveElement && !__qjsTrackedIds[e.__id]) {\n"
	"				__qjsTrackedIds[e.__id] = 1;\n"
	"				/* deferred: the C side also re-defers while the page\n"
	"				 * is still loading - belt and braces for fast hardware */\n"
	"				globalThis.setTimeout(function () {\n"
	"					globalThis.__linksRemoveElement(e.__id);\n"
	"				}, 250);\n"
	"			}\n"
	"		};\n"
	"		e.remove = kill;\n"
	"		e.parentNode = { removeChild: kill, appendChild: function () {} };\n"
	"		if (id && globalThis.__qjsTrackedNodes.length < 50)\n"
	"			globalThis.__qjsTrackedNodes.push(e);\n"
	"		return e;\n"
	"	}\n"
	"	var d = globalThis.document;\n"
	"	if (d) {\n"
	"		d.nodeType = 9;\n"
	"		d.documentElement = globalThis.__qjsDomDocel || elem();\n"
	"		d.body = globalThis.__qjsDomBody || elem();\n"
	"		d.head = globalThis.__qjsDomHead || elem();\n"
	"		if (globalThis.__qjsDomNode) {\n"
	"			d.createElement = function (t) { return new globalThis.__qjsDomNode(t); };\n"
	"			d.createDocumentFragment = function () { return new globalThis.__qjsDomNode(\"#fragment\"); };\n"
	"			d.createTextNode = function (t) {\n"
	"				var T = function () {};  /* DomText-like */\n"
	"				return { nodeType: 3, data: String(t), childNodes: [], parentNode: null };\n"
	"			};\n"
	"		} else {\n"
	"			d.createElement = elem;\n"
	"			d.createDocumentFragment = elem;\n"
	"			d.createTextNode = function (t) { return { nodeType: 3, data: t }; };\n"
	"		}\n"
	"		d.getElementsByClassName = function () { return []; };\n"
	"		d.getElementsByName = function () { return []; };\n"
	"		d.querySelector = function (sel) {\n"
	"			/* JS tree first (SPA-built nodes), then page-source elements */\n"
	"			var hit = null;\n"
	"			try { hit = globalThis.__qjsDomBody.querySelector(sel); } catch (e) {}\n"
	"			if (hit) return hit;\n"
	"			var m = /^#([A-Za-z0-9_-]+)$/.exec(sel || \"\");\n"
	"			return trackedElem(m ? m[1] : null);\n"
	"		};\n"
	"		d.getElementById = function (id) {\n"
	"			var hit = globalThis.__qjsDomBody.getElementById(id);\n"
	"			if (hit) return hit;\n"
	"			return trackedElem(id);\n"
	"		};\n"
	"		d.querySelectorAll = function () { return []; };\n"
	"		d.getElementsByTagName = function (t) {\n"
	"			var tl = (t || \"\").toLowerCase();\n"
	"			if (tl === \"body\") return [d.body || elem()];\n"
	"			if (tl === \"head\") return [d.head || elem()];\n"
	"			if (tl === \"html\") return [d.documentElement || elem()];\n"
	"			return [];\n"
	"		};\n"
	"		d.addEventListener = function () {};\n"
	"		d.removeEventListener = function () {};\n"
	"		d.implementation = {\n"
	"			hasFeature: function () { return true; },\n"
	"			createHTMLDocument: function () {\n"
	"				var b = elem();\n"
	"				b.childNodes = [elem(), elem()];\n"
	"				return { body: b };\n"
	"			}\n"
	"		};\n"
	"		d.readyState = \"complete\";\n"
	"		d.domain = \"\";\n"
	"		d.characterSet = \"utf-8\";\n"
	"		d.visibilityState = \"visible\";\n"
	"		d.hidden = false;\n"
	"	}\n"
	"\n"
	"	var w = globalThis.window;\n"
	"	if (w) {\n"
	"		w.document = d;\n"
	"		if (w.navigator === undefined && globalThis.navigator) w.navigator = globalThis.navigator;\n"
	"		w.scrollTo = function () {};\n"
	"		w.scrollBy = function () {};\n"
	"		w.scrollX = 0; w.scrollY = 0;\n"
	"		w.focus = function () {}; w.blur = function () {};\n"
	"		w.print = function () {};\n"
	"		w.postMessage = function () {};\n"
	"		w.dispatchEvent = function () { return true; };\n"
	"		w.getComputedStyle = function () { return { getPropertyValue: function () { return \"\"; } }; };\n"
	"		w.matchMedia = function () { return { matches: false, addListener: function () {}, addEventListener: function () {} }; };\n"
	"		w.addEventListener = function () {};\n"
	"		w.removeEventListener = function () {};\n"
	"		w.open = function () { return null; };\n"
	"		w.close = function () {};\n"
	"		w.scroll = function () {};\n"
	"		/* must actually fire: React's scheduler drives renders off\n"
	"		 * rAF/MessageChannel; a never-firing rAF means SPAs never paint */\n"
	"		w.requestAnimationFrame = function (f) { return globalThis.setTimeout(function () { f(Date.now()); }, 16); };\n"
	"		w.cancelAnimationFrame = function () {};\n"
	"		w.innerWidth = 80; w.innerHeight = 25;\n"
	"		w.outerWidth = 80; w.outerHeight = 25;\n"
	"		w.screenX = 0; w.screenY = 0;\n"
	"		w.pageXOffset = 0; w.pageYOffset = 0;\n"
	"		w.history = { length: 1, back: function () {}, forward: function () {}, go: function () {} };\n"
	"		w.screen = { width: 640, height: 400, colorDepth: 4 };\n"
	"		w.name = \"\";\n"
	"	}\n"
	"\n"
	"	/* timers: setTimeout is REAL (C-implemented via Links install_timer);\n"
	"	 * setInterval still a no-op stub */\n"
	"\n"
	"	globalThis.getComputedStyle = globalThis.getComputedStyle ||\n"
	"		function () { return { getPropertyValue: function () { return \"\"; } }; };\n"
	"	globalThis.matchMedia = globalThis.matchMedia ||\n"
	"		function () { return { matches: false, addListener: function () {} }; };\n"
	"	globalThis.requestAnimationFrame = function () { return 0; };\n"
	"	globalThis.cancelAnimationFrame = function () {};\n"
	"\n"
	"	globalThis.HTMLElement = function HTMLElement() { throw new TypeError(\"Illegal constructor\"); };\n"
	"	globalThis.Element = function Element() { throw new TypeError(\"Illegal constructor\"); };\n"
	"	globalThis.Node = function Node() { throw new TypeError(\"Illegal constructor\"); };\n"
	"	globalThis.customElements = globalThis.customElements || {\n"
	"		define: function () {}, get: function () { return undefined; },\n"
	"		whenDefined: function () { return Promise.resolve(); }\n"
	"	};\n"
	"	globalThis.CustomEvent = globalThis.CustomEvent || function (t) { this.type = t; };\n"
	"	globalThis.Event = globalThis.Event || function (t) { this.type = t; };\n"
	"	globalThis.MutationObserver = globalThis.MutationObserver ||\n"
	"		function () { this.observe = function () {}; this.disconnect = function () {}; };\n"
	"	globalThis.IntersectionObserver = globalThis.IntersectionObserver ||\n"
	"		function () { this.observe = function () {}; this.disconnect = function () {}; };\n"
	"	globalThis.performance = globalThis.performance || { now: function () { return 0; } };\n"
	"\n"
	"	/* URLSearchParams (needed by bld-webcomponents.js) */\n"
	"	globalThis.URLSearchParams = globalThis.URLSearchParams || function (init) {\n"
	"		var m = {};\n"
	"		if (typeof init === \"string\" && init)\n"
	"			init.replace(/^\\?/, \"\").split(\"&\").forEach(function (kv) {\n"
	"				var p = kv.split(\"=\");\n"
	"				var dec = function (s) { return decodeURIComponent(String(s).replace(/\\+/g, \" \")); };\n"
	"				if (p[0]) m[dec(p[0])] = dec(p[1] || \"\");\n"
	"			});\n"
	"		return {\n"
	"			get: function (k) { return k in m ? m[k] : null; },\n"
	"			getAll: function (k) { return k in m ? [m[k]] : []; },\n"
	"			has: function (k) { return k in m; },\n"
	"			set: function (k, v) { m[k] = v; },\n"
	"			append: function (k, v) { m[k] = v; },\n"
	"			delete: function (k) { delete m[k]; },\n"
	"			forEach: function (fn) { for (var k in m) fn(m[k], k); },\n"
	"			toString: function () {\n"
	"				var s = [];\n"
	"				for (var k in m) s.push(encodeURIComponent(k) + \"=\" + encodeURIComponent(m[k]));\n"
	"				return s.join(\"&\");\n"
	"			}\n"
	"		};\n"
	"	};\n"
	"\n"
	"	/* URL (needed by bld-search.js: new URL(location.href)) */\n"
	"	if (!globalThis.URL) {\n"
	"		globalThis.URL = function (href, base) {\n"
	"			if (base && !/^[a-z]+:\\/\\//i.test(href)) {\n"
	"				var b = new globalThis.URL(base);\n"
	"				if (href.charAt(0) === \"/\")\n"
	"					href = b.origin + href;\n"
	"				else {\n"
	"					var dir = b.pathname.replace(/[^/]*$/, \"\");\n"
	"					href = b.origin + dir + href;\n"
	"				}\n"
	"			}\n"
	"			var m = /^(?:([a-z]+):)?\\/\\/?([^/?#]*)([^?#]*)(\\?[^#]*)?(#.*)?/i.exec(href) || [];\n"
	"			var host = m[2] || \"\", path = m[3] || \"/\";\n"
	"			var pi = host.indexOf(\":\");\n"
	"			this.protocol = (m[1] || \"http\") + \":\";\n"
	"			this.host = host;\n"
	"			this.hostname = pi >= 0 ? host.slice(0, pi) : host;\n"
	"			this.port = pi >= 0 ? host.slice(pi + 1) : \"\";\n"
	"			this.pathname = path;\n"
	"			this.search = m[4] || \"\";\n"
	"			this.hash = m[5] || \"\";\n"
	"			this.href = href;\n"
	"			this.origin = this.protocol + \"//\" + this.host;\n"
	"			var sp = new URLSearchParams(this.search);\n"
	"			this.searchParams = sp;\n"
	"			this.toString = function () { return this.href; };\n"
	"		};\n"
	"	}\n"
	"	/* location.search / location.hash (bld-search reads ?q=) */\n"
	"	if (globalThis.location) {\n"
	"		(function () {\n"
	"			var loc = globalThis.location;\n"
	"			try {\n"
	"				Object.defineProperty(loc, \"search\", {\n"
	"					get: function () {\n"
	"						var h = this.href, i = h.indexOf(\"?\");\n"
	"						return i < 0 ? \"\" : h.slice(i).replace(/#.*$/, \"\");\n"
	"					}, configurable: true\n"
	"				});\n"
	"				Object.defineProperty(loc, \"hash\", {\n"
	"					get: function () {\n"
	"						var h = this.href, i = h.indexOf(\"#\");\n"
	"						return i < 0 ? \"\" : h.slice(i);\n"
	"					}, configurable: true\n"
	"				});\n"
	"			} catch (e) {}\n"
	"		})();\n"
	"	}\n"
	"\n"
	"	/* XMLHttpRequest: synchronous via native __linksHttp */\n"
	"	if (!globalThis.XMLHttpRequest && globalThis.__linksHttp) {\n"
	"		globalThis.XMLHttpRequest = function () {\n"
	"			var self = this;\n"
	"			this.readyState = 0;\n"
	"			this.status = 0;\n"
	"			this.statusText = \"\";\n"
	"			this.responseText = \"\";\n"
	"			this.response = \"\";\n"
	"			this.onreadystatechange = null;\n"
	"			this.onload = null;\n"
	"			this.onerror = null;\n"
	"			var _m = \"GET\", _u = \"\", _h = {}, _sent = false;\n"
	"			this.open = function (m, u, a) { _m = m; _u = u; this.readyState = 1; };\n"
	"			this.setRequestHeader = function (k, v) { _h[k] = v; };\n"
	"			this.getAllResponseHeaders = function () { return \"\"; };\n"
	"			this.getResponseHeader = function () { return null; };\n"
	"			this.abort = function () {};\n"
	"			this.send = function (body) {\n"
	"				if (_sent) return;\n"
	"				_sent = true;\n"
	"				var ctype = _h[\"Content-Type\"] || _h[\"content-type\"] || null;\n"
	"				var absu = _u;\n"
	"				try { absu = new globalThis.URL(_u, globalThis.location && globalThis.location.href).href; } catch (e) {}\n"
	"				var r = globalThis.__linksHttp(absu, _m, ctype,\n"
	"					typeof body === \"string\" ? body : (body == null ? null : String(body)));\n"
	"				self.readyState = 4;\n"
	"				if (r) {\n"
	"					self.status = r.status;\n"
	"					self.responseText = r.body;\n"
	"					self.response = r.body;\n"
	"				} else {\n"
	"					self.status = 0;\n"
	"				}\n"
	"				if (typeof self.onreadystatechange === \"function\")\n"
	"					try { self.onreadystatechange(); } catch (e) {}\n"
	"				if (typeof self.onload === \"function\")\n"
	"					try { self.onload(); } catch (e) {}\n"
	"			};\n"
	"		};\n"
	"	}\n"
	"\n"
	"	/* ---------------- event dispatch + form-submit search ---------------- */\n"
	"	globalThis.__qjsEvents = {};\n"
	"	globalThis.__qjsAddEventListener = function (type, fn) {\n"
	"		(globalThis.__qjsEvents[type] = globalThis.__qjsEvents[type] || []).push(fn);\n"
	"	};\n"
	"	if (w) {\n"
	"		w.addEventListener = function (t, fn) { globalThis.__qjsAddEventListener(t, fn); };\n"
	"	}\n"
	"	if (d) {\n"
	"		d.addEventListener = function (t, fn) { globalThis.__qjsAddEventListener(t, fn); };\n"
	"	}\n"
	"\n"
	"	/* generic event dispatch for runtime-registered listeners\n"
	"	 * (click etc.) - called from C when the user activates a link or\n"
	"	 * button. Returns defaultPrevented via __qjsPreventDefault. */\n"
	"	globalThis.__qjsDispatch = function (type) {\n"
	"		var ev = {\n"
	"			type: type || \"click\",\n"
	"			target: d ? d.body : null,\n"
	"			defaultPrevented: false,\n"
	"			preventDefault: function () { this.defaultPrevented = true; },\n"
	"			stopPropagation: function () {}\n"
	"		};\n"
	"		var list = (globalThis.__qjsEvents[type] || []).slice();\n"
	"		for (var i = 0; i < list.length; i++) {\n"
	"			try { list[i](ev); } catch (e) {}\n"
	"		}\n"
	"		globalThis.__qjsPreventDefault = !!ev.defaultPrevented;\n"
	"	};\n"
	"\n"
	"	globalThis.__qjsOnFormSubmit = function () {\n"
	"		var ev = {\n"
	"			type: \"submit\", target: d ? d.body : null,\n"
	"			defaultPrevented: false,\n"
	"			preventDefault: function () { this.defaultPrevented = true; },\n"
	"			stopPropagation: function () {}\n"
	"		};\n"
	"		var list = (globalThis.__qjsEvents[\"submit\"] || []).slice();\n"
	"		for (var i = 0; i < list.length; i++) {\n"
	"			try { list[i](ev); } catch (e) {}\n"
	"		}\n"
	"		/* URL-state design: let the native GET submit navigate to\n"
	"		 * zoeken?q=... so Links pushes a REAL history entry. The\n"
	"		 * auto-search timer on that page renders the results; the\n"
	"		 * Back key then returns to zoeken?q=... and re-renders. */\n"
	"		globalThis.__qjsPreventDefault = false;\n"
	"	};\n"
	"\n"
	"	/* Fallback search: renders vinden.belastingdienst.nl results as real\n"
	"	 * document.write HTML so they are VISIBLE in Links. Only for forms\n"
	"	 * whose encoded data contains a q= field. */\n"
	"	globalThis.__qjsFallbackSearch = function () {\n"
	"		var params = new URLSearchParams(globalThis.__qjsFormRaw || \"\");\n"
	"		var q = params.get(\"q\");\n"
	"		if (!q) return;\n"
	"		var body = {\n"
	"			sort_date_facets_by_value: true, max_page_count: 100,\n"
	"			content_sample_length: 300, count: 100,\n"
	"			show_query_spelling_alternatives: true,\n"
	"			properties: [\n"
	"				{ formats: [\"VALUE\", \"HTML\"], name: \"title\" },\n"
	"				{ formats: [\"VALUE\", \"HTML\"], name: \"path\" },\n"
	"				{ formats: [\"VALUE\", \"HTML\"], name: \"url\" },\n"
	"				{ name: \"description\", formats: [\"VALUE\", \"HTML\"] }\n"
	"			],\n"
	"			paging_states: [],\n"
	"			query_context: {\n"
	"				app_tab_id: \"Everything\", application_id: \"Default Application\",\n"
	"				query_id: \"qjs\" + Date.now(), prev_query_id: null,\n"
	"				query_trigger_type: \"USER_QUERY\", query_trigger_action: \"manual_search\"\n"
	"			},\n"
	"			query_context_user_query: q,\n"
	"			user: { query: { and: [{ unparsed: q, id: \"query\" }], constraints: [] } },\n"
	"			user_context: {\n"
	"				referer: globalThis.location ? globalThis.location.href : \"\",\n"
	"				locale: \"nl\", service_id: \"\",\n"
	"				utc_time_zone_differential_in_seconds: 3600\n"
	"			}\n"
	"		};\n"
	"		globalThis.fetch(\"https://vinden.belastingdienst.nl/api/v2/search\", {\n"
	"			method: \"POST\",\n"
	"			headers: { \"Content-Type\": \"application/json; charset=utf-8\" },\n"
	"			body: JSON.stringify(body)\n"
	"		}).then(function (resp) {\n"
	"			return resp.json();\n"
	"		}).then(function (j) {\n"
	"			var n = j.estimated_count;\n"
	"			var res = (j.resultset && j.resultset.results) || [];\n"
	"			var out = \"<h2>Zoekresultaten voor '\" + q + \"' (\" + n + \" gevonden)</h2>\";\n"
	"			if (!res.length) out += \"<p>Geen resultaten.</p>\";\n"
	"			for (var i = 0; i < res.length; i++) {\n"
	"				var props = res[i].properties || [];\n"
	"				var title = \"\", url = \"\", desc = \"\";\n"
	"				for (var k = 0; k < props.length; k++) {\n"
	"					var p = props[k];\n"
	"					var dat = p.data && p.data[0];\n"
	"					var txt = dat ? (dat.html != null ? dat.html :\n"
	"						(dat.value != null ? dat.value : \"\")) : \"\";\n"
	"					if (typeof txt === \"object\" && txt !== null)\n"
	"						txt = txt.str != null ? txt.str : \"\";\n"
	"					if (p.id === \"title\") title = String(txt);\n"
	"					else if (p.id === \"url\" && !url) url = String(txt);\n"
	"					else if (p.id === \"description\") desc = String(txt);\n"
	"				}\n"
	"				if (!url) url = res[i].id || \"\";\n"
	"				if (url.indexOf(\"http\") !== 0)\n"
	"					url = \"https://www.belastingdienst.nl/\" + url;\n"
	"				var esc = function (s) {\n"
	"					return String(s).replace(/&/g, \"&amp;\")\n"
	"						.replace(/</g, \"&lt;\").replace(/\"/g, \"&quot;\");\n"
	"				};\n"
	"				/* the API entity-encodes values (&#x2F; etc.): decode first */\n"
	"				var unesc = function (s) {\n"
	"					return String(s)\n"
	"						.replace(/&#x([0-9a-f]+);/gi, function (m, h) {\n"
	"							return String.fromCharCode(parseInt(h, 16));\n"
	"						})\n"
	"						.replace(/&#([0-9]+);/g, function (m, d) {\n"
	"							return String.fromCharCode(parseInt(d, 10));\n"
	"						})\n"
	"						.replace(/&amp;/g, \"&\").replace(/&lt;/g, \"<\")\n"
	"						.replace(/&gt;/g, \">\").replace(/&quot;/g, '\"')\n"
	"						.replace(/&#x27;/g, \"'\");\n"
	"				};\n"
	"				title = unesc(title);\n"
	"				desc = unesc(desc);\n"
	"				var href = unesc(url).replace(/<[^>]*>/g, \"\");\n"
	"				if (href.indexOf(\"http\") !== 0)\n"
	"					href = \"https://www.belastingdienst.nl/\" + href;\n"
	"				out += \"<p><b>\" + (i + 1) + \".</b> \" +\n"
	"					'<a href=\"' + esc(href) + '\">' + (title || esc(href)) + \"</a><br>\" +\n"
	"					desc + \"<br>\" + esc(href) + \"</p>\";\n"
	"			}\n"
	"			out += \"<p>QJS-SEARCH-END</p>\";\n"
	"			if (d.__qjsReplacePage)\n"
	"				d.__qjsReplacePage(out);\n"
	"			else\n"
	"				d.write(out);\n"
	"		}).catch(function (e) {\n"
	"			d.write(\"<p>Zoekfout: \" + e + \"</p><p>QJS-SEARCH-END</p>\");\n"
	"		});\n"
	"	};\n"
	"	/* console: page timers used console.log on hardware */\n"
	"	if (!globalThis.console) {\n"
	"		globalThis.console = {\n"
	"			log: function () {}, info: function () {}, warn: function () {},\n"
	"			error: function () {}, debug: function () {}, trace: function () {}\n"
	"		};\n"
	"	}\n"
	"\n"
	"	/* ---------------- auto-search on zoeken?q= URLs ----------------\n"
	"	 * The search results live at a REAL URL (history/back works):\n"
	"	 * when a page whose path contains \"zoeken\" carries ?q=, fetch and\n"
	"	 * render the results into this page after it settles. */\n"
	"	(function () {\n"
	"		try {\n"
	"			if (!globalThis.location || !globalThis.setTimeout) return;\n"
	"			var host = globalThis.location.hostname || \"\";\n"
	"			if (!/(^|\\.)belastingdienst\\.nl$/.test(host)) return;\n"
	"			/* kpn.com etc. also use /zoeken paths - NEVER run the\n"
	"			 * vinden fallback outside belastingdienst.nl */\n"
	"			var p = globalThis.location.pathname || \"\";\n"
	"			if (p.indexOf(\"zoeken\") < 0) return;\n"
	"			var sp = new URLSearchParams(globalThis.location.search || \"\");\n"
	"			if (!sp.get(\"q\")) return;\n"
	"			globalThis.__qjsAutoSearchDone = false;\n"
	"			globalThis.setTimeout(function () {\n"
	"				if (globalThis.__qjsAutoSearchDone) return;\n"
	"				globalThis.__qjsAutoSearchDone = true;\n"
	"				globalThis.__qjsFormRaw = sp.toString();\n"
	"				try { globalThis.__qjsFallbackSearch(); }\n"
	"				catch (e) {\n"
	"					try {\n"
	"						(d || globalThis.document).write(\n"
	"							\"<p>QJS-SEARCH-ERR \" + e + \"</p>\");\n"
	"					} catch (e2) {}\n"
	"				}\n"
	"			}, 3000);\n"
	"		} catch (e) {}\n"
	"	})();\n"
	"\n"
	"	/* ---------------- challenge-script API surface (Cloudflare etc.) ---- */\n"
	"	(function () {\n"
	"		function illegal(name) {\n"
	"			var f = function () { throw new TypeError(\"Illegal constructor: \" + name); };\n"
	"			f.prototype = Object.create(null);\n"
	"			return f;\n"
	"		}\n"
	"		var classes = [\"HTMLElement\", \"HTMLScriptElement\", \"HTMLInputElement\",\n"
	"			\"HTMLFormElement\", \"HTMLIFrameElement\", \"HTMLImageElement\",\n"
	"			\"HTMLButtonElement\", \"HTMLTextAreaElement\", \"HTMLAnchorElement\",\n"
	"			\"HTMLDivElement\", \"HTMLSpanElement\", \"HTMLCanvasElement\",\n"
	"			\"HTMLBodyElement\", \"HTMLHeadElement\", \"HTMLLinkElement\",\n"
	"			\"HTMLStyleElement\", \"HTMLMetaElement\", \"HTMLTitleElement\",\n"
	"			\"HTMLParagraphElement\", \"HTMLUnknownElement\", \"HTMLTemplateElement\", \"HTMLPictureElement\",\n"
	"			\"HTMLFieldSetElement\", \"HTMLLabelElement\", \"HTMLQuoteElement\",\n"
	"			\"HTMLBRElement\", \"HTMLHRElement\", \"HTMLPreElement\", \"HTMLNavElement\",\n"
	"			\"HTMLOListElement\", \"HTMLLIElement\", \"HTMLMapElement\", \"HTMLAreaElement\",\n"
	"			\"HTMLProgressElement\", \"HTMLMeterElement\", \"HTMLDataListElement\",\n"
	"			\"HTMLOutputElement\", \"HTMLDetailsElement\", \"HTMLSummaryElement\",\n"
	"			\"HTMLDialogElement\", \"HTMLSlotElement\", \"HTMLEmbedElement\",\n"
	"			\"HTMLObjectElement\", \"HTMLVideoElement\", \"HTMLAudioElement\",\n"
	"			\"HTMLSourceElement\", \"HTMLTrackElement\", \"HTMLMarqueeElement\", \"HTMLOptionElement\",\n"
	"			\"HTMLSelectElement\", \"HTMLTableElement\", \"HTMLUListElement\",\n"
	"			\"SVGSVGElement\", \"SVGElement\", \"HTMLCollection\", \"NodeList\",\n"
	"			\"NamedNodeMap\", \"DOMTokenList\", \"Screen\", \"History\", \"Location\",\n"
	"			\"Storage\", \"XMLHttpRequest\", \"Image\", \"Option\", \"FormData\",\n"
	"			\"FileReader\", \"Blob\", \"File\", \"Text\", \"CSSStyleDeclaration\",\n"
	"			\"MediaQueryList\", \"Notification\", \"WebSocket\", \"Worker\",\n"
	"			\"AbortController\", \"AbortSignal\", \"ReadableStream\",\n"
	"			\"WritableStream\", \"TransformStream\", \"TextEncoder\", \"TextDecoder\"];\n"
	"		for (var i = 0; i < classes.length; i++) {\n"
	"			if (!globalThis[classes[i]])\n"
	"				globalThis[classes[i]] = illegal(classes[i]);\n"
	"		}\n"
	"	})();\n"
	"\n"
	"	/* window.crypto: Turnstile reads getRandomValues */\n"
	"	if (!globalThis.crypto) {\n"
	"		globalThis.crypto = {\n"
	"			getRandomValues: function (arr) {\n"
	"				for (var i = 0; i < arr.length; i++)\n"
	"					arr[i] = Math.floor(Math.random() * 256);\n"
	"				return arr;\n"
	"			},\n"
	"			randomUUID: function () {\n"
	"				return \"xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx\".replace(/[xy]/g, function (c) {\n"
	"					var r = Math.random() * 16 | 0;\n"
	"					return (c === \"x\" ? r : (r & 0x3 | 0x8)).toString(16);\n"
	"				});\n"
	"			},\n"
	"			subtle: {}\n"
	"		};\n"
	"	}\n"
	"\n"
	"	/* performance timing */\n"
	"	if (!globalThis.performance || !globalThis.performance.now) {\n"
	"		var __perf0 = Date.now();\n"
	"		globalThis.performance = {\n"
	"			now: function () { return Date.now() - __perf0; },\n"
	"			timeOrigin: __perf0,\n"
	"			mark: function () {}, measure: function () {},\n"
	"			getEntriesByType: function () { return []; },\n"
	"			getEntries: function () { return []; }\n"
	"		};\n"
	"	}\n"
	"\n"
	"	/* navigator extras probed by bot detection */\n"
	"	if (globalThis.navigator) {\n"
	"		var nav = globalThis.navigator;\n"
	"		if (nav.webdriver === undefined) nav.webdriver = false;\n"
	"		if (nav.plugins === undefined) nav.plugins = { length: 0, item: function () { return null; } };\n"
	"		if (nav.mimeTypes === undefined) nav.mimeTypes = { length: 0 };\n"
	"		if (nav.languages === undefined) nav.languages = [\"nl\", \"en\"];\n"
	"		if (nav.hardwareConcurrency === undefined) nav.hardwareConcurrency = 1;\n"
	"		if (nav.maxTouchPoints === undefined) nav.maxTouchPoints = 0;\n"
	"		if (nav.deviceMemory === undefined) nav.deviceMemory = 64;\n"
	"		if (nav.vendor === undefined) nav.vendor = \"\";\n"
	"		if (nav.product === undefined) nav.product = \"Gecko\";\n"
	"		if (nav.sendBeacon === undefined) nav.sendBeacon = function () { return true; };\n"
	"		if (nav.connection === undefined) nav.connection = { effectiveType: \"3g\", downlink: 1, rtt: 300 };\n"
	"		if (nav.getBattery === undefined) nav.getBattery = function () { return Promise.resolve({ charging: false, level: 1 }); };\n"
	"	}\n"
	"\n"
	"	/* document.currentScript (Turnstile reads .dataset on it) */\n"
	"	if (d && d.currentScript === undefined) {\n"
	"		d.currentScript = elem();\n"
	"		d.currentScript.dataset = {};\n"
	"	}\n"
	"	if (d && d.scripts === undefined) d.scripts = [];\n"
	"	if (d && d.contentType === undefined) d.contentType = \"text/html\";\n"
	"	if (d && d.compatMode === undefined) d.compatMode = \"CSS1Compat\";\n"
	"	if (d && d.activeElement === undefined) d.activeElement = d.body || elem();\n"
	"\n"
	"	/* ---------------- property-access tracer ----------------\n"
	"	 * Proxy-wrap document, navigator and the window BINDING: every\n"
	"	 * property the page reads is logged (once) via __qjsTraceLog to\n"
	"	 * C:\\JSTRACE.LOG - reveals what challenge scripts probe. */\n"
	"	(function () {\n"
	"		if (!globalThis.Proxy || !globalThis.__qjsTraceLog) return;\n"
	"		var seen = {};\n"
	"		function T(name, obj) {\n"
	"			try {\n"
	"				return new Proxy(obj, {\n"
	"					get: function (t, k) {\n"
	"						var key = name + \".\" + String(k);\n"
	"						if (!seen[key]) { seen[key] = 1; globalThis.__qjsTraceLog(key); }\n"
	"						return t[k];\n"
	"					}\n"
	"				});\n"
	"			} catch (e) { return obj; }\n"
	"		}\n"
	"		try { globalThis.document = T(\"document\", globalThis.document); } catch (e) {}\n"
	"		try { globalThis.navigator = T(\"navigator\", globalThis.navigator); } catch (e) {}\n"
	"		try { globalThis.window = T(\"window\", globalThis); } catch (e) {}\n"
	"	})();\n"
	"\n"
	"	/* ================= DOM->LINKS RENDER BRIDGE =================\n"
	"	 * A real (small) DOM tree in JS: SPA frameworks (React etc.) build\n"
	"	 * their document via createElement/appendChild/textContent. When\n"
	"	 * such a tree actually gets content under <body>, we serialize it\n"
	"	 * and re-render the whole page (__qjsReplacePage) so the content\n"
	"	 * becomes VISIBLE in Links. Sites that only probe (jQuery) never\n"
	"	 * build a tree -> page untouched. */\n"
	"	(function () {\n"
	"		var __dirty = false, __renderTimer = null, __lastSer = null;\n"
	"\n"
	"		function DomText(t) {\n"
	"			this.nodeType = 3;\n"
	"			this.data = t || \"\";\n"
	"			this.parentNode = null;\n"
	"			this.childNodes = [];\n"
	"		}\n"
	"		DomText.prototype.cloneNode = function () { return new DomText(this.data); };\n"
	"		Object.defineProperty(DomText.prototype, \"textContent\", {\n"
	"			get: function () { return this.data; },\n"
	"			set: function (v) { this.data = String(v); markDirty(); },\n"
	"			configurable: true\n"
	"		});\n"
	"		DomText.prototype.appendChild = function () {};\n"
	"		DomText.prototype.removeChild = function () {};\n"
	"\n"
	"		function DomNode(tag) {\n"
	"			this.nodeType = 1;\n"
	"			this.tagName = String(tag || \"div\").toUpperCase();\n"
	"			this.childNodes = [];\n"
	"			this.parentNode = null;\n"
	"			this.attributes = {};\n"
	"			this.style = {};\n"
	"			this._listeners = [];\n"
	"			this._value = \"\";\n"
	"			this._innerHTML = \"\";\n"
	"			this._text = \"\";\n"
	"			this.ownerDocument = globalThis.document;\n"
	"		}\n"
	"		DomNode.prototype.appendChild = function (n) {\n"
	"			if (n && n.nodeType) {\n"
	"				if (n.parentNode) n.parentNode.removeChild(n);\n"
	"				n.parentNode = this;\n"
	"				this.childNodes.push(n);\n"
	"				markDirty();\n"
	"			}\n"
	"			return n;\n"
	"		};\n"
	"		DomNode.prototype.insertBefore = function (n, ref) {\n"
	"			if (!n || !n.nodeType) return n;\n"
	"			var i = ref ? this.childNodes.indexOf(ref) : -1;\n"
	"			if (n.parentNode) n.parentNode.removeChild(n);\n"
	"			n.parentNode = this;\n"
	"			if (i < 0) this.childNodes.push(n);\n"
	"			else this.childNodes.splice(i, 0, n);\n"
	"			markDirty();\n"
	"			return n;\n"
	"		};\n"
	"		DomNode.prototype.removeChild = function (n) {\n"
	"			var i = this.childNodes.indexOf(n);\n"
	"			if (i >= 0) {\n"
	"				this.childNodes.splice(i, 1);\n"
	"				n.parentNode = null;\n"
	"				markDirty();\n"
	"			}\n"
	"			return n;\n"
	"		};\n"
	"		DomNode.prototype.replaceChild = function (n, o) {\n"
	"			this.insertBefore(n, o);\n"
	"			if (o) this.removeChild(o);\n"
	"			return o;\n"
	"		};\n"
	"		DomNode.prototype.append = function () {\n"
	"			for (var i = 0; i < arguments.length; i++) {\n"
	"				var a = arguments[i];\n"
	"				if (a && a.nodeType) this.appendChild(a);\n"
	"				else this.appendChild(new DomText(String(a)));\n"
	"			}\n"
	"		};\n"
	"		DomNode.prototype.prepend = DomNode.prototype.append;\n"
	"		DomNode.prototype.replaceChildren = function () {\n"
	"			while (this.firstChild) this.removeChild(this.firstChild);\n"
	"			this.append.apply(this, arguments);\n"
	"		};\n"
	"		DomNode.prototype.cloneNode = function (deep) {\n"
	"			var c = new DomNode(this.tagName);\n"
	"			c.attributes = JSON.parse(JSON.stringify(this.attributes || {}));\n"
	"			c._value = this._value; c._text = this._text;\n"
	"			c._innerHTML = this._innerHTML;\n"
	"			if (deep) {\n"
	"				for (var i = 0; i < this.childNodes.length; i++)\n"
	"					c.appendChild(this.childNodes[i].cloneNode(true));\n"
	"			}\n"
	"			return c;\n"
	"		};\n"
	"		DomNode.prototype.setAttribute = function (k, v) {\n"
	"			this.attributes[k] = String(v);\n"
	"			markDirty();\n"
	"		};\n"
	"		DomNode.prototype.getAttribute = function (k) {\n"
	"			return (k in this.attributes) ? this.attributes[k] : null;\n"
	"		};\n"
	"		DomNode.prototype.removeAttribute = function (k) { delete this.attributes[k]; markDirty(); };\n"
	"		DomNode.prototype.hasAttribute = function (k) { return k in this.attributes; };\n"
	"		DomNode.prototype.addEventListener = function (t, fn) {\n"
	"			if (typeof fn === \"function\") {\n"
	"				this._listeners.push(fn);\n"
	"				if (globalThis.__qjsAddEventListener) globalThis.__qjsAddEventListener(t, fn);\n"
	"			}\n"
	"		};\n"
	"		DomNode.prototype.removeEventListener = function () {};\n"
	"		DomNode.prototype.getElementsByTagName = function (t) {\n"
	"			var out = [], tt = String(t).toUpperCase(), i;\n"
	"			for (i = 0; i < this.childNodes.length; i++) {\n"
	"				var c = this.childNodes[i];\n"
	"				if (c.nodeType === 1) {\n"
	"					if (tt === \"*\" || c.tagName === tt) out.push(c);\n"
	"					out = out.concat(c.getElementsByTagName(t));\n"
	"				}\n"
	"			}\n"
	"			return out;\n"
	"		};\n"
	"		DomNode.prototype.getElementById = function (id) {\n"
	"			var i, c;\n"
	"			for (i = 0; i < this.childNodes.length; i++) {\n"
	"				c = this.childNodes[i];\n"
	"				if (c.nodeType === 1) {\n"
	"					if (c.attributes && c.attributes.id === id) return c;\n"
	"					var r = c.getElementById(id);\n"
	"					if (r) return r;\n"
	"				}\n"
	"			}\n"
	"			return null;\n"
	"		};\n"
	"		DomNode.prototype.contains = function (n) {\n"
	"			while (n) { if (n === this) return true; n = n.parentNode; }\n"
	"			return false;\n"
	"		};\n"
	"		DomNode.prototype.closest = function () { return null; };\n"
	"		DomNode.prototype.matches = function () { return false; };\n"
	"		DomNode.prototype.focus = function () {};\n"
	"		DomNode.prototype.blur = function () {};\n"
	"		DomNode.prototype.click = function () {\n"
	"			var l = this._listeners.slice(), i;\n"
	"			for (i = 0; i < l.length; i++) { try { l[i]({type:\"click\",target:this}); } catch (e) {} }\n"
	"		};\n"
	"		DomNode.prototype.getContext = function () { return {}; };\n"
	"		DomNode.prototype.toDataURL = function () { return \"data:,\"; };\n"
	"		DomNode.prototype.getBoundingClientRect = function () {\n"
	"			return { top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0, x: 0, y: 0 };\n"
	"		};\n"
	"		DomNode.prototype.scrollIntoView = function () {};\n"
	"		DomNode.prototype.insertAdjacentHTML = function () { markDirty(); };\n"
	"		DomNode.prototype.dispatchEvent = function () { return true; };\n"
	"		DomNode.prototype.querySelector = function (sel) {\n"
	"			var all = __qjsQueryAll(this, sel);\n"
	"			return all.length ? all[0] : null;\n"
	"		};\n"
	"		DomNode.prototype.querySelectorAll = function (sel) {\n"
	"			return __qjsQueryAll(this, sel);\n"
	"		};\n"
	"\n"
	"		/* property accessors via defineProperty */\n"
	"		(function () {\n"
	"			var proto = DomNode.prototype;\n"
	"			Object.defineProperty(proto, \"firstChild\", {\n"
	"				get: function () { return this.childNodes[0] || null; },\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"lastChild\", {\n"
	"				get: function () { return this.childNodes[this.childNodes.length - 1] || null; },\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"nextSibling\", {\n"
	"				get: function () {\n"
	"					if (!this.parentNode) return null;\n"
	"					var i = this.parentNode.childNodes.indexOf(this);\n"
	"					return this.parentNode.childNodes[i + 1] || null;\n"
	"				},\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"previousSibling\", {\n"
	"				get: function () {\n"
	"					if (!this.parentNode) return null;\n"
	"					var i = this.parentNode.childNodes.indexOf(this);\n"
	"					return i > 0 ? this.parentNode.childNodes[i - 1] : null;\n"
	"				},\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"children\", {\n"
	"				get: function () {\n"
	"					return this.childNodes.filter(function (c) { return c.nodeType === 1; });\n"
	"				},\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"childElementCount\", {\n"
	"				get: function () { return this.children.length; },\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"id\", {\n"
	"				get: function () { return this.attributes.id || \"\"; },\n"
	"				set: function (v) { this.attributes.id = String(v); markDirty(); },\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"className\", {\n"
	"				get: function () { return this.attributes[\"class\"] || \"\"; },\n"
	"				set: function (v) { this.attributes[\"class\"] = String(v); markDirty(); },\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"value\", {\n"
	"				get: function () { return this._value; },\n"
	"				set: function (v) { this._value = String(v); markDirty(); },\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"dataset\", {\n"
	"				get: function () {\n"
	"					var ds = {}, k;\n"
	"					for (k in this.attributes)\n"
	"						if (k.indexOf(\"data-\") === 0)\n"
	"							ds[k.slice(5).replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); })] = this.attributes[k];\n"
	"					return ds;\n"
	"				},\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"classList\", {\n"
	"				get: function () {\n"
	"					var self = this;\n"
	"					var list = (self.attributes[\"class\"] || \"\").split(/\\s+/).filter(Boolean);\n"
	"					return {\n"
	"						add: function (c) { if (list.indexOf(c) < 0) list.push(c); self.attributes[\"class\"] = list.join(\" \"); markDirty(); },\n"
	"						remove: function (c) { list = list.filter(function (x) { return x !== c; }); self.attributes[\"class\"] = list.join(\" \"); markDirty(); },\n"
	"						toggle: function (c) { this.add(c); },\n"
	"						contains: function (c) { return list.indexOf(c) >= 0; }\n"
	"					};\n"
	"				},\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"textContent\", {\n"
	"				get: function () {\n"
	"					var s = \"\", i;\n"
	"					for (i = 0; i < this.childNodes.length; i++)\n"
	"						s += (this.childNodes[i].nodeType === 3) ? this.childNodes[i].data\n"
	"							: (this.childNodes[i].textContent || \"\");\n"
	"					return s;\n"
	"				},\n"
	"				set: function (v) {\n"
	"					while (this.firstChild) this.removeChild(this.firstChild);\n"
	"					this.appendChild(new DomText(String(v)));\n"
	"				},\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"innerText\", {\n"
	"				get: function () { return this.textContent; },\n"
	"				set: function (v) { this.textContent = v; },\n"
	"				configurable: true\n"
	"			});\n"
	"			Object.defineProperty(proto, \"innerHTML\", {\n"
	"				get: function () { return this._innerHTML; },\n"
	"				set: function (v) {\n"
	"					this._innerHTML = String(v);\n"
	"					while (this.firstChild) this.removeChild(this.firstChild);\n"
	"					try { parseHTMLInto(String(v), this); } catch (e) {}\n"
	"					markDirty();\n"
	"				},\n"
	"				configurable: true\n"
	"			});\n"
	"		})();\n"
	"\n"
	"		/* tiny HTML parser for innerHTML values */\n"
	"		function parseHTMLInto(html, parent) {\n"
	"			var stack = [parent], pos = 0, m;\n"
	"			var tagRe = /<(\\/)?([a-zA-Z][a-zA-Z0-9]*)((?:[^>\"']|\"[^\"]*\"|'[^']*')*?)(\\/?)>/g;\n"
	"			var esc = function (s) { return s; }; /* text kept raw */\n"
	"			while ((m = tagRe.exec(html)) !== null) {\n"
	"				if (m.index > pos) {\n"
	"					var txt = html.slice(pos, m.index).replace(/&nbsp;/g, \" \").replace(/&amp;/g, \"&\");\n"
	"					if (txt.trim()) stack[stack.length - 1].appendChild(new DomText(txt));\n"
	"				}\n"
	"				var closing = m[1] === \"/\", name = m[2].toUpperCase(), selfc = m[4] === \"/\";\n"
	"				if (closing) {\n"
	"					for (var k = stack.length - 1; k > 0; k--)\n"
	"						if (stack[k].tagName === name) { stack.length = k; break; }\n"
	"				} else if (!selfc && name !== \"BR\" && name !== \"HR\" && name !== \"IMG\" && name !== \"INPUT\" && name !== \"META\" && name !== \"LINK\") {\n"
	"					var n = new DomNode(name);\n"
	"					var attrRe = /([a-zA-Z_:][-a-zA-Z0-9_:.]*)\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|[^\\s>]+)/g;\n"
	"					var am, attrs = m[3] || \"\";\n"
	"					while ((am = attrRe.exec(attrs)) !== null)\n"
	"						n.attributes[am[1]] = (am[3] !== undefined ? am[3] : (am[4] !== undefined ? am[4] : am[2]));\n"
	"					stack[stack.length - 1].appendChild(n);\n"
	"					stack.push(n);\n"
	"				} else {\n"
	"					var sn = new DomNode(name);\n"
	"					stack[stack.length - 1].appendChild(sn);\n"
	"				}\n"
	"				pos = tagRe.lastIndex;\n"
	"			}\n"
	"			if (pos < html.length) {\n"
	"				var tail = html.slice(pos).replace(/&nbsp;/g, \" \").replace(/&amp;/g, \"&\");\n"
	"				if (tail.trim()) stack[stack.length - 1].appendChild(new DomText(tail));\n"
	"			}\n"
	"		}\n"
	"\n"
	"		/* very small selector engine: \"tag\", \"#id\", \".class\", \"tag.cls\" */\n"
	"		function __qjsQueryAll(root, sel) {\n"
	"			var out = [];\n"
	"			if (!sel) return out;\n"
	"			sel = String(sel).split(\",\")[0].trim();\n"
	"			var m = /^(\\w+)?(?:#([\\w-]+))?(?:\\.([\\w-]+))?$/.exec(sel);\n"
	"			function walk(n) {\n"
	"				var i, c;\n"
	"				for (i = 0; i < n.childNodes.length; i++) {\n"
	"					c = n.childNodes[i];\n"
	"					if (c.nodeType === 1) {\n"
	"						var ok = true;\n"
	"						if (m) {\n"
	"							if (m[1] && c.tagName !== m[1].toUpperCase()) ok = false;\n"
	"							if (ok && m[2] && c.attributes.id !== m[2]) ok = false;\n"
	"							if (ok && m[3] && (\" \" + (c.attributes[\"class\"] || \"\") + \" \").indexOf(\" \" + m[3] + \" \") < 0) ok = false;\n"
	"						} else ok = false;\n"
	"						if (ok) out.push(c);\n"
	"						walk(c);\n"
	"					}\n"
	"				}\n"
	"			}\n"
	"			walk(root);\n"
	"			return out;\n"
	"		}\n"
	"\n"
	"		/* serializer: tree -> HTML for Links */\n"
	"		function escText(s) {\n"
	"			return String(s).replace(/&/g, \"&amp;\").replace(/</g, \"&lt;\").replace(/>/g, \"&gt;\");\n"
	"		}\n"
	"		function escAttr(s) {\n"
	"			return escText(s).replace(/\"/g, \"&quot;\");\n"
	"		}\n"
	"		function serialize(n) {\n"
	"			if (n.nodeType === 3) return escText(n.data);\n"
	"			if (n.nodeType !== 1) return \"\";\n"
	"			var t = n.tagName, h = \"<\" + t, k;\n"
	"			for (k in n.attributes)\n"
	"				if (k === \"id\" || k === \"class\" || k === \"href\" || k === \"src\" || k === \"type\" || k === \"name\")\n"
	"					h += \" \" + k + '=\"' + escAttr(n.attributes[k]) + '\"';\n"
	"			h += \">\";\n"
	"			if (t === \"SCRIPT\" || t === \"STYLE\" || t === \"TEMPLATE\") return h + \"</\" + t + \">\";\n"
	"			if (t === \"INPUT\") {\n"
	"				var ph = n.attributes.placeholder || n._value || \"\";\n"
	"				if (ph) h += escText(ph);\n"
	"				return h + \"</\" + t + \">\";\n"
	"			}\n"
	"			var i;\n"
	"			for (i = 0; i < n.childNodes.length; i++)\n"
	"				h += serialize(n.childNodes[i]);\n"
	"			h += \"</\" + t + \">\";\n"
	"			return h;\n"
	"		}\n"
	"\n"
	"		function markDirty() {\n"
	"			__dirty = true;\n"
	"			if (__renderTimer || !globalThis.setTimeout) return;\n"
	"			__renderTimer = true;\n"
	"			globalThis.setTimeout(function () {\n"
	"				__renderTimer = false;\n"
	"				if (!__dirty) return;\n"
	"				__dirty = false;\n"
	"				var body = globalThis.__qjsDomBody;\n"
	"				if (!body) return;\n"
	"				var ser = serialize(body);\n"
	"				/* SPA roots: page-static containers (e.g. <div id=root>)\n"
	"				 * that scripts populated - they hang outside our body\n"
	"				 * tree, so serialize them too */\n"
	"				var tn = globalThis.__qjsTrackedNodes || [], k;\n"
	"				for (k = 0; k < tn.length; k++) {\n"
	"					var n = tn[k];\n"
	"					if (n.childElementCount === 0) continue;\n"
	"					var anc = n, inBody = false;\n"
	"					while (anc) { if (anc === body) { inBody = true; break; } anc = anc.parentNode; }\n"
	"					if (!inBody) ser += serialize(n);\n"
	"				}\n"
	"				if (!body.childElementCount && ser.length === 0) return;\n"
	"				/* only render SUBSTANTIAL trees: jQuery-driven sites\n"
	"				 * append stray probe nodes to body all the time -\n"
	"				 * replacing a full page with those blanks it */\n"
	"				if (!ser || ser.length < 600 || ser === __lastSer) return;\n"
	"				if (globalThis.__qjsTraceLog)\n"
	"					globalThis.__qjsTraceLog(\"DOM-RENDER len=\" + ser.length);\n"
	"				__lastSer = ser;\n"
	"				var doc = globalThis.document;\n"
	"				if (doc && doc.__qjsReplacePage) {\n"
	"					doc.__qjsReplacePage(\n"
	"						\"<h1>\" + escText((globalThis.document && globalThis.document.title) || \"\") + \"</h1>\" + ser);\n"
	"				}\n"
	"			}, 700);\n"
	"		}\n"
	"\n"
	"		/* body/head/documentElement become real nodes; keep permissive\n"
	"		 * page-source removal for ids that are NOT in the JS tree */\n"
	"		var __body = new DomNode(\"body\");\n"
	"		var __head = new DomNode(\"head\");\n"
	"		var __docel = new DomNode(\"html\");\n"
	"		__docel.appendChild(__head);\n"
	"		__docel.appendChild(__body);\n"
	"		globalThis.__qjsDomBody = __body;\n"
	"		globalThis.__qjsDomHead = __head;\n"
	"		globalThis.__qjsDomDocel = __docel;\n"
	"		globalThis.__qjsDomNode = DomNode;\n"
	"		globalThis.__qjsDomText = DomText;\n"
	"		globalThis.__qjsParseHTMLInto = parseHTMLInto;\n"
	"\n"
	"		/* re-wire document (this block runs AFTER the document setup\n"
	"		 * above, which had to fall back to the permissive stubs) */\n"
	"		if (globalThis.document) {\n"
	"			var dd = globalThis.document;\n"
	"			dd.createElement = function (t) { return new DomNode(t); };\n"
	"			dd.createDocumentFragment = function () { return new DomNode(\"#fragment\"); };\n"
	"			dd.createTextNode = function (t) { return new (globalThis.__qjsDomText)(t); };\n"
	"			dd.body = __body;\n"
	"			dd.head = __head;\n"
	"			dd.documentElement = __docel;\n"
	"			dd.getElementById = function (id) {\n"
	"				var hit = __body.getElementById(id);\n"
	"				if (hit) return hit;\n"
	"				return trackedElem(id);\n"
	"			};\n"
	"			dd.querySelector = function (sel) {\n"
	"				var hit = null;\n"
	"				try { hit = __body.querySelector(sel); } catch (e) {}\n"
	"				if (hit) return hit;\n"
	"				var m = /^#([A-Za-z0-9_-]+)$/.exec(sel || \"\");\n"
	"				return trackedElem(m ? m[1] : null);\n"
	"			};\n"
	"			dd.querySelectorAll = function (sel) {\n"
	"				var r;\n"
	"				try { r = __body.querySelectorAll(sel); } catch (e) { r = []; }\n"
	"				return (r && r.length) ? r : [];\n"
	"			};\n"
	"			dd.getElementsByTagName = function (t) {\n"
	"				var r = __body.getElementsByTagName(t);\n"
	"				return (r && r.length) ? r : [];\n"
	"			};\n"
	"		}\n"
	"	})();\n"
	"\n"
	"\n"
	"	/* React scheduler primitives */\n"
	"	if (!globalThis.MessageChannel) {\n"
	"		globalThis.MessageChannel = function () {\n"
	"			var self = this;\n"
	"			this.port1 = {\n"
	"				postMessage: function (d) { globalThis.setTimeout(function () { if (self.port1.onmessage) self.port1.onmessage({ data: d }); }, 0); }\n"
	"			};\n"
	"			this.port2 = {\n"
	"				postMessage: function (d) { globalThis.setTimeout(function () { if (self.port2.onmessage) self.port2.onmessage({ data: d }); }, 0); }\n"
	"			};\n"
	"		};\n"
	"	}\n"
	"	if (!globalThis.requestIdleCallback) {\n"
	"		globalThis.requestIdleCallback = function (f) {\n"
	"			return globalThis.setTimeout(function () {\n"
	"				f({ didTimeout: false, timeRemaining: function () { return 50; } });\n"
	"			}, 50);\n"
	"		};\n"
	"		globalThis.cancelIdleCallback = function () {};\n"
	"	}\n"
	"	if (!globalThis.queueMicrotask) {\n"
	"		globalThis.queueMicrotask = function (f) { Promise.resolve().then(f); };\n"
	"	}\n"
	"})();\n"
	"\n";

/* QJS-BIGTEST v2: instrument QuickJS heap usage to learn how much
 * the 2.6MB bundle parse actually needs, and what DPMI provides. */
#include <dpmi.h>
static unsigned long qjs_mem_used, qjs_mem_next_log = 16UL * 1024 * 1024;

static void qjs_mem_log(unsigned long used)
{
	char mb[128];
	__dpmi_free_mem_info mi;
	unsigned long dpmi_free = 0;
	extern void sock_log2(const char *);
	if (__dpmi_get_free_memory_information(&mi) == 0)
		dpmi_free = (unsigned long)mi.largest_available_free_block_in_bytes;
	snprintf(mb, sizeof mb,
		"QJS-MEM used=%luMB dpmi_largest_free=%luKB",
		used >> 20, dpmi_free >> 10);
	sock_log2(mb);
}

/* Hooks maintain EXACT accounting (8-byte size-header wrap); the LIMIT
 * itself is enforced by QuickJS's own tested JS_SetMemoryLimit path
 * (js_malloc checks malloc_size vs memory_limit BEFORE calling the
 * hook and throws a clean MemoryError). Returning NULL from the hooks
 * directly (v3) hit unchecked parse paths -> General Protection Fault.
 */
#define QJS_HDR 8

static void *qjs_jm_malloc(JSMallocState *s, size_t n)
{
	char *p;
	if (n > (size_t)-1 - QJS_HDR - 16) return NULL;
	p = (char *)malloc(n + QJS_HDR);
	if (p) {
		memcpy(p, &n, sizeof n);
		s->malloc_count++;
		s->malloc_size += n;
		qjs_mem_used = s->malloc_size;
		if (qjs_mem_used >= qjs_mem_next_log) {
			qjs_mem_log(qjs_mem_used);
			qjs_mem_next_log += 8UL * 1024 * 1024;
		}
		return p + QJS_HDR;
	}
	return NULL;
}
static void qjs_jm_free(JSMallocState *s, void *vp)
{
	char *p = (char *)vp;
	if (p) {
		size_t n;
		memcpy(&n, p - QJS_HDR, sizeof n);
		s->malloc_count--;
		s->malloc_size -= n;
		qjs_mem_used = s->malloc_size;
		free(p - QJS_HDR);
	}
}
static void *qjs_jm_realloc(JSMallocState *s, void *vp, size_t n)
{
	char *p = (char *)vp, *q;
	size_t ou = 0;
	if (p) memcpy(&ou, p - QJS_HDR, sizeof ou);
	if (n > (size_t)-1 - QJS_HDR - 16) return NULL;
	q = (char *)realloc(p ? p - QJS_HDR : NULL, n + QJS_HDR);
	if (q) {
		memcpy(q, &n, sizeof n);
		if (!p) s->malloc_count++;
		s->malloc_size += n;
		s->malloc_size -= ou;
		qjs_mem_used = s->malloc_size;
		return q + QJS_HDR;
	}
	return NULL;
}

static const JSMallocFunctions qjs_jm_funcs = {
	qjs_jm_malloc, qjs_jm_free, qjs_jm_realloc
};

/* ---------------- engine interface ---------------- */

struct javascript_context *js_create_context(void *p, long id)
{
	struct javascript_context *c;

	(void)p; (void)id;
	c = mem_alloc(sizeof(struct javascript_context));
	if (!c) return NULL;
	memset(c, 0, sizeof(struct javascript_context));
	c->ptr = p;
	c->id = id;
	c->js_id = ++qjs_context_counter;

	{
		__dpmi_free_mem_info mi;
		char mb[128];
		extern void sock_log2(const char *);
		if (__dpmi_get_free_memory_information(&mi) == 0) {
			snprintf(mb, sizeof mb,
				"QJS-MEM ctx-create dpmi_total=%luKB largest=%luKB",
				(unsigned long)mi.total_number_of_physical_pages * 4UL,
				(unsigned long)mi.largest_available_free_block_in_bytes >> 10);
			sock_log2(mb);
		}
	}
	c->rt = JS_NewRuntime2(&qjs_jm_funcs, NULL);
	if (!c->rt) { mem_free(c); return NULL; }
	/* QJS-BIGTEST v4: 384MB - under the measured 472MB hardware
	 * commit ceiling; enforced by QuickJS's own clean-exthrow path */
	JS_SetMemoryLimit(c->rt, 384L * 1024 * 1024);
	JS_SetGCThreshold(c->rt, 256 * 1024);
	JS_SetMaxStackSize(c->rt, 512 * 1024);
	c->ctx = JS_NewContext(c->rt);
	if (!c->ctx) { JS_FreeRuntime(c->rt); mem_free(c); return NULL; }
	JS_SetContextOpaque(c->ctx, c);
	JS_SetInterruptHandler(c->rt, qjs_interrupt_handler, c);
	register_globals(c->ctx);
	{
		/* DOM polyfill: document.nodeType=9 + rich fake elements etc.,
		 * required by jQuery 3.6 (Sizzle setDocument) — see dom_bootstrap.js */
		JSValue r;
		qjs_script_deadline = get_time() + qjs_script_budget(
			(int)(sizeof(qjs_dom_bootstrap) - 1));
		r = JS_Eval(c->ctx, qjs_dom_bootstrap,
			sizeof(qjs_dom_bootstrap) - 1, "<dom_bootstrap>",
			JS_EVAL_TYPE_GLOBAL);
		if (JS_IsException(r)) {
			/* log: a silent bootstrap failure halves the DOM
			 * surface with no diagnostic trail */
			JSValue e = JS_GetException(c->ctx);
			const char *es = JS_ToCString(c->ctx, e);
			FILE *f = fopen("C:\\JSERROR.LOG", "a");
			if (f) {
				fprintf(f, "BOOTSTRAP-FAIL: %s\n", es ? es : "?");
				fclose(f);
			}
			if (es) JS_FreeCString(c->ctx, es);
			JS_FreeValue(c->ctx, e);
		}
	}
	return c;
}

void js_destroy_context(struct javascript_context *c)
{
	if (!c) return;
	c->dead = 1;
	if (c->ctx) JS_FreeContext(c->ctx);
	if (c->rt) {
		JS_RunGC(c->rt);
		JS_FreeRuntime(c->rt);
	}
	if (c->cookies) js_mem_free(c->cookies);
	mem_free(c);
}

void js_execute_code(struct javascript_context *c, unsigned char *code,
		     int len, void (*done)(void *))
{
	unsigned char *z;
	JSValue result;

	if (!c || !code || len < 0) {
		if (done) done(c ? c->ptr : NULL);
		return;
	}
	/* Giant bundles (hn.algolia 2.6MB webpack): parsing needs 10-25x
	 * the source size in QuickJS memory - that exhausts DPMI swap and
	 * KILLS the whole process ("No swap space!"). Skip them and stay
	 * alive; such SPAs cannot run on DOS-class memory anyway. */
	/* QJS-BIGTEST: giant-script guard DISABLED for this test build */
	if (len > 1000000) {
		char mb[96];
		extern void sock_log2(const char *);
		snprintf(mb, sizeof mb,
			"QJS-BIGTEST attempting giant script len=%d", len);
		sock_log2(mb);
	}

	z = mem_alloc((size_t)len + 1);
	if (!z) {
		if (done) done(c->ptr);
		return;
	}
	memcpy(z, code, (size_t)len);
	z[len] = 0;

	/* ES modules (import/export) cannot be evaluated in global scope:
		 * JS_Eval would throw SyntaxError and pollute the log. Skip them —
		 * caught both inline `import {...}` blocks and ESM src files. */
	{
		int i;
		for (i = 0; i < len && (z[i] == ' ' || z[i] == '\t' ||
			z[i] == '\n' || z[i] == '\r'); i++)
			;
		if (i + 6 <= len && !memcmp(z + i, "import", 6) &&
		    (z[i + 6] == ' ' || z[i + 6] == '{' || z[i + 6] == '\n') ||
		    (i + 6 <= len && !memcmp(z + i, "export", 6) &&
		     (z[i + 6] == ' ' || z[i + 6] == '{' || z[i + 6] == '\n' ||
		      z[i + 6] == 'd' /*export default*/))) {
			if (done) done(c->ptr);
			mem_free(z);
			return;
		}
	}

	qjs_script_deadline = get_time() + qjs_script_budget(len);
	result = JS_Eval(c->ctx, (const char *)z, (size_t)len, "<script>",
			 JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(result)) {
		/* report at most one exception per context: unbounded fopen churn
		 * showed up as EMFILE-like symptoms in long sessions */
		if (c->logged_error < 5) {
			c->logged_error++;
			JSValue ex = JS_GetException(c->ctx);
			const char *exs = JS_ToCString(c->ctx, ex);
			FILE *f = fopen("C:\\JSERROR.LOG", "a");
			if (f) {
				/* identify the failing script: first line of code, plus
				 * error class + stack — indispensable for multi-script pages */
				int li;
				const char *stk = NULL;
				JSValue so = JS_GetPropertyStr(c->ctx, ex, "stack");
				if (!JS_IsUndefined(so) && !JS_IsException(so))
					stk = JS_ToCString(c->ctx, so);
				for (li = 0; li < len && z[li] != '\n' && z[li] != '\r'; li++)
					;
				fprintf(f, "%s | first-line=%.*s\n%s\n", exs ? exs : "?",
					 li > 60 ? 60 : li, (const char *)z,
					 stk ? stk : "");
				if (stk) JS_FreeCString(c->ctx, stk);
				JS_FreeValue(c->ctx, so);
				fclose(f);
			}
			if (exs) JS_FreeCString(c->ctx, exs);
			JS_FreeValue(c->ctx, ex);
			c->logged_error = 1;
		} else {
			JS_FreeValue(c->ctx, JS_GetException(c->ctx));
		}
		(void)0;
		JS_FreeValue(c->ctx, result);  /* free the exception marker too */
	} else {
		JS_FreeValue(c->ctx, result);
	}

	/* run pending jobs to completion: fetch() resolves promises
	 * synchronously and .then()/await continuations must execute now,
	 * inside this script step (no event loop exists) */
	qjs_pump_jobs(c);

	/* GC discipline: collect after every script block */
	JS_RunGC(c->rt);

	mem_free(z);
	if (done) done(c->ptr);
}

/* ---------------- old async-protocol entry points (no-ops) -------------- */

void js_downcall_vezmi_true(void *context) { (void)context; }
void js_downcall_vezmi_false(void *context) { (void)context; }
void js_downcall_vezmi_null(void *context) { (void)context; }
void js_downcall_quiet_game_over(void *context) { (void)context; }
void js_downcall_vezmi_string(void *context, unsigned char *string)
{
	(void)context;
	if (string) mem_free(string);
}
void js_spec_vykill_timer(void *context, int a) { (void)context; (void)a; }

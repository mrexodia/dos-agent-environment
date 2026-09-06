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

static int qjs_interrupt_handler(JSRuntime *rt, void *opaque)
{
	(void)rt; (void)opaque;
	/* NOTE: pumping tcp_tick() here corrupted watt32 state when called
	 * mid-connection-processing (marathon regression) - handler kept as
	 * a no-op hook for future script time-caps. */
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
		JSValue r = JS_Call(q->c->ctx, q->func, JS_UNDEFINED, 1, &q->arg);
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
	"			focus: function () {}\n"
	"		};\n"
	"	}\n"
	"	globalThis.__qjs_elem = elem;\n"
	"\n"
	"	var d = globalThis.document;\n"
	"	if (d) {\n"
	"		d.nodeType = 9;\n"
	"		d.documentElement = elem();\n"
	"		d.body = elem();\n"
	"		d.head = elem();\n"
	"		d.createElement = elem;\n"
	"		d.createDocumentFragment = elem;\n"
	"		d.createTextNode = function (t) { return { nodeType: 3, data: t }; };\n"
	"		d.getElementsByClassName = function () { return []; };\n"
	"		d.getElementsByName = function () { return []; };\n"
	"		d.querySelector = function () { return elem(); };\n"
	"		d.getElementById = function () { return elem(); };\n"
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
	"		w.getComputedStyle = function () { return { getPropertyValue: function () { return \"\"; } }; };\n"
	"		w.matchMedia = function () { return { matches: false, addListener: function () {}, addEventListener: function () {} }; };\n"
	"		w.addEventListener = function () {};\n"
	"		w.removeEventListener = function () {};\n"
	"		w.open = function () { return null; };\n"
	"		w.close = function () {};\n"
	"		w.scroll = function () {};\n"
	"		w.requestAnimationFrame = function () { return 0; };\n"
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
	"})();\n"
	"\n";

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

	c->rt = JS_NewRuntime();
	if (!c->rt) { mem_free(c); return NULL; }
	/* keep the DJGPP heap sane: cap the JS heap, force eager GC */
	JS_SetMemoryLimit(c->rt, 4 * 1024 * 1024);
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
		JSValue r = JS_Eval(c->ctx, qjs_dom_bootstrap,
			sizeof(qjs_dom_bootstrap) - 1, "<dom_bootstrap>",
			JS_EVAL_TYPE_GLOBAL);
		if (JS_IsException(r))
			JS_FreeValue(c->ctx, JS_GetException(c->ctx));
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

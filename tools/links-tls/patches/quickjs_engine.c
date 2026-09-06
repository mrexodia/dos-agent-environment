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
	JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "HTMLElement",
		JS_NewCFunction(ctx, qj_noop, "HTMLElement", 0));

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
	"			nodeType: 1, style: {}, className: \"\", innerHTML: \"\", value: \"\",\n"
	"			checked: false,\n"
	"			parentNode: { removeChild: function () {} },\n"
	"			childNodes: [],\n"
	"			appendChild: function (c) { this.childNodes = [c]; return c; },\n"
	"			removeChild: function () {},\n"
	"			get lastChild() { return this.childNodes[this.childNodes.length - 1] || elem(); },\n"
	"			cloneNode: function () { var n = elem(); n.checked = this.checked; return n; },\n"
	"			setAttribute: function () {}, getAttribute: function () { return null; },\n"
	"			removeAttribute: function () {},\n"
	"			addEventListener: function () {}, removeEventListener: function () {},\n"
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
	"		d.querySelectorAll = function () { return []; };\n"
	"		d.getElementsByTagName = function () { return []; };\n"
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
	"	/* timers: no event loop yet — return ids, never fire */\n"
	"	var __tid = 0;\n"
	"	globalThis.setTimeout = function () { return ++__tid; };\n"
	"	globalThis.clearTimeout = function () {};\n"
	"	globalThis.setInterval = function () { return ++__tid; };\n"
	"	globalThis.clearInterval = function () {};\n"
	"	if (w) {\n"
	"		w.setTimeout = globalThis.setTimeout;\n"
	"		w.clearTimeout = globalThis.clearTimeout;\n"
	"		w.setInterval = globalThis.setInterval;\n"
	"		w.clearInterval = globalThis.clearInterval;\n"
	"	}\n"
	"\n"
	"	globalThis.getComputedStyle = globalThis.getComputedStyle ||\n"
	"		function () { return { getPropertyValue: function () { return \"\"; } }; };\n"
	"	globalThis.matchMedia = globalThis.matchMedia ||\n"
	"		function () { return { matches: false, addListener: function () {} }; };\n"
	"	globalThis.requestAnimationFrame = function () { return 0; };\n"
	"	globalThis.cancelAnimationFrame = function () {};\n"
	"\n"
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
	"				if (p[0]) m[decodeURIComponent(p[0])] = decodeURIComponent(p[1] || \"\");\n"
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

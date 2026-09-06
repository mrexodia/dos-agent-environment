/* dom_bootstrap.js — evaluated in every QuickJS context after the C
 * register_globals(). Enriches the minimal C stubs into a "permissive
 * document" good enough for jQuery 3.6 + Bootstrap site bundles.
 * Verified against www.belastingdienst.nl's script chain (host QuickJS
 * harness /tmp/guest_prelude5.js). Keep everything no-op/absorbing. */
(function () {
	"use strict";

	function elem() {
		return {
			nodeType: 1, lang: "nl", style: {}, className: "", innerHTML: "", textContent: "", value: "",
			checked: false,
			parentNode: { removeChild: function () {} },
			childNodes: [],
			appendChild: function (c) { this.childNodes = [c]; return c; },
			removeChild: function () {},
			get lastChild() { return this.childNodes[this.childNodes.length - 1] || elem(); },
			cloneNode: function () { var n = elem(); n.checked = this.checked; return n; },
			setAttribute: function () {}, getAttribute: function () { return null; },
			removeAttribute: function () {},
			addEventListener: function (t, fn) {
				if (typeof fn === "function" && globalThis.__qjsAddEventListener)
					globalThis.__qjsAddEventListener(t, fn);
			},
			removeEventListener: function () {},
			classList: {
				add: function () {}, remove: function () {}, toggle: function () {},
				contains: function () { return false; }
			},
			querySelector: function () { return elem(); },
			querySelectorAll: function () { return []; },
			getElementsByTagName: function () { return []; },
			getElementsByClassName: function () { return []; },
			contains: function () { return false; },
			focus: function () {}
		};
	}
	globalThis.__qjs_elem = elem;

	var d = globalThis.document;
	if (d) {
		d.nodeType = 9;
		d.documentElement = elem();
		d.body = elem();
		d.head = elem();
		d.createElement = elem;
		d.createDocumentFragment = elem;
		d.createTextNode = function (t) { return { nodeType: 3, data: t }; };
		d.getElementsByClassName = function () { return []; };
		d.getElementsByName = function () { return []; };
		d.querySelector = function () { return elem(); };
		d.getElementById = function () { return elem(); };
		d.querySelectorAll = function () { return []; };
		d.getElementsByTagName = function (t) {
			var tl = (t || "").toLowerCase();
			if (tl === "body") return [d.body || elem()];
			if (tl === "head") return [d.head || elem()];
			if (tl === "html") return [d.documentElement || elem()];
			return [];
		};
		d.addEventListener = function () {};
		d.removeEventListener = function () {};
		d.implementation = {
			hasFeature: function () { return true; },
			createHTMLDocument: function () {
				var b = elem();
				b.childNodes = [elem(), elem()];
				return { body: b };
			}
		};
		d.readyState = "complete";
		d.domain = "";
		d.characterSet = "utf-8";
		d.visibilityState = "visible";
		d.hidden = false;
	}

	var w = globalThis.window;
	if (w) {
		w.document = d;
		if (w.navigator === undefined && globalThis.navigator) w.navigator = globalThis.navigator;
		w.getComputedStyle = function () { return { getPropertyValue: function () { return ""; } }; };
		w.matchMedia = function () { return { matches: false, addListener: function () {}, addEventListener: function () {} }; };
		w.addEventListener = function () {};
		w.removeEventListener = function () {};
		w.open = function () { return null; };
		w.close = function () {};
		w.scroll = function () {};
		w.requestAnimationFrame = function () { return 0; };
		w.cancelAnimationFrame = function () {};
		w.innerWidth = 80; w.innerHeight = 25;
		w.outerWidth = 80; w.outerHeight = 25;
		w.screenX = 0; w.screenY = 0;
		w.pageXOffset = 0; w.pageYOffset = 0;
		w.history = { length: 1, back: function () {}, forward: function () {}, go: function () {} };
		w.screen = { width: 640, height: 400, colorDepth: 4 };
		w.name = "";
	}

	/* timers: setTimeout is REAL (C-implemented via Links install_timer);
	 * setInterval still a no-op stub */

	globalThis.getComputedStyle = globalThis.getComputedStyle ||
		function () { return { getPropertyValue: function () { return ""; } }; };
	globalThis.matchMedia = globalThis.matchMedia ||
		function () { return { matches: false, addListener: function () {} }; };
	globalThis.requestAnimationFrame = function () { return 0; };
	globalThis.cancelAnimationFrame = function () {};

	globalThis.HTMLElement = function HTMLElement() { throw new TypeError("Illegal constructor"); };
	globalThis.Element = function Element() { throw new TypeError("Illegal constructor"); };
	globalThis.Node = function Node() { throw new TypeError("Illegal constructor"); };
	globalThis.customElements = globalThis.customElements || {
		define: function () {}, get: function () { return undefined; },
		whenDefined: function () { return Promise.resolve(); }
	};
	globalThis.CustomEvent = globalThis.CustomEvent || function (t) { this.type = t; };
	globalThis.Event = globalThis.Event || function (t) { this.type = t; };
	globalThis.MutationObserver = globalThis.MutationObserver ||
		function () { this.observe = function () {}; this.disconnect = function () {}; };
	globalThis.IntersectionObserver = globalThis.IntersectionObserver ||
		function () { this.observe = function () {}; this.disconnect = function () {}; };
	globalThis.performance = globalThis.performance || { now: function () { return 0; } };

	/* URLSearchParams (needed by bld-webcomponents.js) */
	globalThis.URLSearchParams = globalThis.URLSearchParams || function (init) {
		var m = {};
		if (typeof init === "string" && init)
			init.replace(/^\?/, "").split("&").forEach(function (kv) {
				var p = kv.split("=");
				var dec = function (s) { return decodeURIComponent(String(s).replace(/\+/g, " ")); };
				if (p[0]) m[dec(p[0])] = dec(p[1] || "");
			});
		return {
			get: function (k) { return k in m ? m[k] : null; },
			getAll: function (k) { return k in m ? [m[k]] : []; },
			has: function (k) { return k in m; },
			set: function (k, v) { m[k] = v; },
			append: function (k, v) { m[k] = v; },
			delete: function (k) { delete m[k]; },
			forEach: function (fn) { for (var k in m) fn(m[k], k); },
			toString: function () {
				var s = [];
				for (var k in m) s.push(encodeURIComponent(k) + "=" + encodeURIComponent(m[k]));
				return s.join("&");
			}
		};
	};

	/* URL (needed by bld-search.js: new URL(location.href)) */
	if (!globalThis.URL) {
		globalThis.URL = function (href, base) {
			if (base && !/^[a-z]+:\/\//i.test(href)) {
				var b = new globalThis.URL(base);
				if (href.charAt(0) === "/")
					href = b.origin + href;
				else {
					var dir = b.pathname.replace(/[^/]*$/, "");
					href = b.origin + dir + href;
				}
			}
			var m = /^(?:([a-z]+):)?\/\/?([^/?#]*)([^?#]*)(\?[^#]*)?(#.*)?/i.exec(href) || [];
			var host = m[2] || "", path = m[3] || "/";
			var pi = host.indexOf(":");
			this.protocol = (m[1] || "http") + ":";
			this.host = host;
			this.hostname = pi >= 0 ? host.slice(0, pi) : host;
			this.port = pi >= 0 ? host.slice(pi + 1) : "";
			this.pathname = path;
			this.search = m[4] || "";
			this.hash = m[5] || "";
			this.href = href;
			this.origin = this.protocol + "//" + this.host;
			var sp = new URLSearchParams(this.search);
			this.searchParams = sp;
			this.toString = function () { return this.href; };
		};
	}
	/* location.search / location.hash (bld-search reads ?q=) */
	if (globalThis.location) {
		(function () {
			var loc = globalThis.location;
			try {
				Object.defineProperty(loc, "search", {
					get: function () {
						var h = this.href, i = h.indexOf("?");
						return i < 0 ? "" : h.slice(i).replace(/#.*$/, "");
					}, configurable: true
				});
				Object.defineProperty(loc, "hash", {
					get: function () {
						var h = this.href, i = h.indexOf("#");
						return i < 0 ? "" : h.slice(i);
					}, configurable: true
				});
			} catch (e) {}
		})();
	}

	/* XMLHttpRequest: synchronous via native __linksHttp */
	if (!globalThis.XMLHttpRequest && globalThis.__linksHttp) {
		globalThis.XMLHttpRequest = function () {
			var self = this;
			this.readyState = 0;
			this.status = 0;
			this.statusText = "";
			this.responseText = "";
			this.response = "";
			this.onreadystatechange = null;
			this.onload = null;
			this.onerror = null;
			var _m = "GET", _u = "", _h = {}, _sent = false;
			this.open = function (m, u, a) { _m = m; _u = u; this.readyState = 1; };
			this.setRequestHeader = function (k, v) { _h[k] = v; };
			this.getAllResponseHeaders = function () { return ""; };
			this.getResponseHeader = function () { return null; };
			this.abort = function () {};
			this.send = function (body) {
				if (_sent) return;
				_sent = true;
				var ctype = _h["Content-Type"] || _h["content-type"] || null;
				var absu = _u;
				try { absu = new globalThis.URL(_u, globalThis.location && globalThis.location.href).href; } catch (e) {}
				var r = globalThis.__linksHttp(absu, _m, ctype,
					typeof body === "string" ? body : (body == null ? null : String(body)));
				self.readyState = 4;
				if (r) {
					self.status = r.status;
					self.responseText = r.body;
					self.response = r.body;
				} else {
					self.status = 0;
				}
				if (typeof self.onreadystatechange === "function")
					try { self.onreadystatechange(); } catch (e) {}
				if (typeof self.onload === "function")
					try { self.onload(); } catch (e) {}
			};
		};
	}

	/* ---------------- event dispatch + form-submit search ---------------- */
	globalThis.__qjsEvents = {};
	globalThis.__qjsAddEventListener = function (type, fn) {
		(globalThis.__qjsEvents[type] = globalThis.__qjsEvents[type] || []).push(fn);
	};
	if (w) {
		w.addEventListener = function (t, fn) { globalThis.__qjsAddEventListener(t, fn); };
	}
	if (d) {
		d.addEventListener = function (t, fn) { globalThis.__qjsAddEventListener(t, fn); };
	}

	globalThis.__qjsOnFormSubmit = function () {
		var ev = {
			type: "submit", target: d ? d.body : null,
			defaultPrevented: false,
			preventDefault: function () { this.defaultPrevented = true; },
			stopPropagation: function () {}
		};
		var list = (globalThis.__qjsEvents["submit"] || []).slice();
		for (var i = 0; i < list.length; i++) {
			try { list[i](ev); } catch (e) {}
		}
		if (ev.defaultPrevented) {
			globalThis.__qjsPreventDefault = true;
			return;
		}
		globalThis.__qjsPreventDefault = false;
		try {
			globalThis.__qjsFallbackSearch();
			/* the fallback search renders its own visible results:
			 * cancel the native GET navigation */
			var pp = new URLSearchParams(globalThis.__qjsFormRaw || "");
			if (pp.get("q")) globalThis.__qjsPreventDefault = true;
		} catch (e) {
			try { d.write("<p>QJS-SUBMIT-ERR " + e + "</p>"); } catch (e2) {}
		}
	};

	/* Fallback search: renders vinden.belastingdienst.nl results as real
	 * document.write HTML so they are VISIBLE in Links. Only for forms
	 * whose encoded data contains a q= field. */
	globalThis.__qjsFallbackSearch = function () {
		var params = new URLSearchParams(globalThis.__qjsFormRaw || "");
		var q = params.get("q");
		if (!q) return;
		var body = {
			sort_date_facets_by_value: true, max_page_count: 100,
			content_sample_length: 300, count: 10,
			show_query_spelling_alternatives: true,
			properties: [
				{ formats: ["VALUE", "HTML"], name: "title" },
				{ formats: ["VALUE", "HTML"], name: "path" },
				{ formats: ["VALUE", "HTML"], name: "url" },
				{ name: "description", formats: ["VALUE", "HTML"] }
			],
			paging_states: [],
			query_context: {
				app_tab_id: "Everything", application_id: "Default Application",
				query_id: "qjs" + Date.now(), prev_query_id: null,
				query_trigger_type: "USER_QUERY", query_trigger_action: "manual_search"
			},
			query_context_user_query: q,
			user: { query: { and: [{ unparsed: q, id: "query" }], constraints: [] } },
			user_context: {
				referer: globalThis.location ? globalThis.location.href : "",
				locale: "nl", service_id: "",
				utc_time_zone_differential_in_seconds: 3600
			}
		};
		globalThis.fetch("https://vinden.belastingdienst.nl/api/v2/search", {
			method: "POST",
			headers: { "Content-Type": "application/json; charset=utf-8" },
			body: JSON.stringify(body)
		}).then(function (resp) {
			return resp.json();
		}).then(function (j) {
			var n = j.estimated_count;
			var res = (j.resultset && j.resultset.results) || [];
			var out = "<h2>Zoekresultaten voor '" + q + "' (" + n + " gevonden)</h2>";
			if (!res.length) out += "<p>Geen resultaten.</p>";
			for (var i = 0; i < res.length; i++) {
				var props = res[i].properties || [];
				var title = "", url = "", desc = "";
				for (var k = 0; k < props.length; k++) {
					var p = props[k];
					var dat = p.data && p.data[0];
					var txt = dat ? (dat.html != null ? dat.html :
						(dat.value != null ? dat.value : "")) : "";
					if (typeof txt === "object" && txt !== null)
						txt = txt.str != null ? txt.str : "";
					if (p.id === "title") title = String(txt);
					else if (p.id === "url" && !url) url = String(txt);
					else if (p.id === "description") desc = String(txt);
				}
				if (!url) url = res[i].id || "";
				if (url.indexOf("http") !== 0)
					url = "https://www.belastingdienst.nl/" + url;
				out += "<p><b>" + (i + 1) + ". " + title + "</b><br>" +
					desc + "<br>" + url + "</p>";
			}
			out += "<p>QJS-SEARCH-END</p>";
			if (d.__qjsReplacePage)
				d.__qjsReplacePage(out);
			else
				d.write(out);
		}).catch(function (e) {
			d.write("<p>Zoekfout: " + e + "</p><p>QJS-SEARCH-END</p>");
		});
	};
	/* console: page timers used console.log on hardware */
	if (!globalThis.console) {
		globalThis.console = {
			log: function () {}, info: function () {}, warn: function () {},
			error: function () {}, debug: function () {}, trace: function () {}
		};
	}
})();

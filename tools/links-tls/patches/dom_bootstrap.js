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
			focus: function () {}, blur: function () {},
			getBoundingClientRect: function () {
				return { top: 0, left: 0, right: 0, bottom: 0,
					width: 0, height: 0, x: 0, y: 0 };
			},
			matches: function () { return false; },
			closest: function () { return null; },
			getComputedStyle: function () { return {}; },
			scrollIntoView: function () {},
			insertAdjacentHTML: function () {},
			replaceChildren: function () {},
			/* canvas stub: belastingdienst.js noHTML5() probes
			 * createElement("canvas").getContext - without it the
			 * site keeps the 'Javascript staat uit' block */
			getContext: function () {
				return {
					fillRect: function () {}, clearRect: function () {},
					getImageData: function () { return { data: [] }; },
					putImageData: function () {}, drawImage: function () {},
					measureText: function () { return { width: 0 }; },
					beginPath: function () {}, arc: function () {},
					fill: function () {}, stroke: function () {},
					save: function () {}, restore: function () {},
					translate: function () {}, scale: function () {}
				};
			},
			toDataURL: function () { return "data:,"; }
		};
	}
	globalThis.__qjs_elem = elem;

	var __qjsTrackedIds = {};
	globalThis.__qjsTrackedNodes = [];
	function trackedElem(id) {
		var e = globalThis.__qjsDomNode ? new globalThis.__qjsDomNode("div") : elem();
		e.__id = id || null;
		var kill = function () {
			if (e.__id && globalThis.__linksRemoveElement && !__qjsTrackedIds[e.__id]) {
				__qjsTrackedIds[e.__id] = 1;
				/* deferred: the C side also re-defers while the page
				 * is still loading - belt and braces for fast hardware */
				globalThis.setTimeout(function () {
					globalThis.__linksRemoveElement(e.__id);
				}, 250);
			}
		};
		e.remove = kill;
		e.parentNode = { removeChild: kill, appendChild: function () {} };
		if (id && globalThis.__qjsTrackedNodes.length < 50)
			globalThis.__qjsTrackedNodes.push(e);
		return e;
	}
	var d = globalThis.document;
	if (d) {
		d.nodeType = 9;
		d.documentElement = globalThis.__qjsDomDocel || elem();
		d.body = globalThis.__qjsDomBody || elem();
		d.head = globalThis.__qjsDomHead || elem();
		if (globalThis.__qjsDomNode) {
			d.createElement = function (t) { return new globalThis.__qjsDomNode(t); };
			d.createDocumentFragment = function () { return new globalThis.__qjsDomNode("#fragment"); };
			d.createTextNode = function (t) {
				var T = function () {};  /* DomText-like */
				return { nodeType: 3, data: String(t), childNodes: [], parentNode: null };
			};
		} else {
			d.createElement = elem;
			d.createDocumentFragment = elem;
			d.createTextNode = function (t) { return { nodeType: 3, data: t }; };
		}
		d.getElementsByClassName = function () { return []; };
		d.getElementsByName = function () { return []; };
		d.querySelector = function (sel) {
			/* JS tree first (SPA-built nodes), then page-source elements */
			var hit = null;
			try { hit = globalThis.__qjsDomBody.querySelector(sel); } catch (e) {}
			if (hit) return hit;
			var m = /^#([A-Za-z0-9_-]+)$/.exec(sel || "");
			return trackedElem(m ? m[1] : null);
		};
		d.getElementById = function (id) {
			var hit = globalThis.__qjsDomBody.getElementById(id);
			if (hit) return hit;
			return trackedElem(id);
		};
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
		w.scrollTo = function () {};
		w.scrollBy = function () {};
		w.scrollX = 0; w.scrollY = 0;
		w.focus = function () {}; w.blur = function () {};
		w.print = function () {};
		w.postMessage = function () {};
		w.dispatchEvent = function () { return true; };
		w.getComputedStyle = function () { return { getPropertyValue: function () { return ""; } }; };
		w.matchMedia = function () { return { matches: false, addListener: function () {}, addEventListener: function () {} }; };
		w.addEventListener = function () {};
		w.removeEventListener = function () {};
		w.open = function () { return null; };
		w.close = function () {};
		w.scroll = function () {};
		/* must actually fire: React's scheduler drives renders off
		 * rAF/MessageChannel; a never-firing rAF means SPAs never paint */
		w.requestAnimationFrame = function (f) { return globalThis.setTimeout(function () { f(Date.now()); }, 16); };
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

	/* generic event dispatch for runtime-registered listeners
	 * (click etc.) - called from C when the user activates a link or
	 * button. Returns defaultPrevented via __qjsPreventDefault. */
	globalThis.__qjsDispatch = function (type) {
		var ev = {
			type: type || "click",
			target: d ? d.body : null,
			defaultPrevented: false,
			preventDefault: function () { this.defaultPrevented = true; },
			stopPropagation: function () {}
		};
		var list = (globalThis.__qjsEvents[type] || []).slice();
		for (var i = 0; i < list.length; i++) {
			try { list[i](ev); } catch (e) {}
		}
		globalThis.__qjsPreventDefault = !!ev.defaultPrevented;
	};

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
		/* URL-state design: let the native GET submit navigate to
		 * zoeken?q=... so Links pushes a REAL history entry. The
		 * auto-search timer on that page renders the results; the
		 * Back key then returns to zoeken?q=... and re-renders. */
		globalThis.__qjsPreventDefault = false;
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
			content_sample_length: 300, count: 100,
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
				var esc = function (s) {
					return String(s).replace(/&/g, "&amp;")
						.replace(/</g, "&lt;").replace(/"/g, "&quot;");
				};
				/* the API entity-encodes values (&#x2F; etc.): decode first */
				var unesc = function (s) {
					return String(s)
						.replace(/&#x([0-9a-f]+);/gi, function (m, h) {
							return String.fromCharCode(parseInt(h, 16));
						})
						.replace(/&#([0-9]+);/g, function (m, d) {
							return String.fromCharCode(parseInt(d, 10));
						})
						.replace(/&amp;/g, "&").replace(/&lt;/g, "<")
						.replace(/&gt;/g, ">").replace(/&quot;/g, '"')
						.replace(/&#x27;/g, "'");
				};
				title = unesc(title);
				desc = unesc(desc);
				var href = unesc(url).replace(/<[^>]*>/g, "");
				if (href.indexOf("http") !== 0)
					href = "https://www.belastingdienst.nl/" + href;
				out += "<p><b>" + (i + 1) + ".</b> " +
					'<a href="' + esc(href) + '">' + (title || esc(href)) + "</a><br>" +
					desc + "<br>" + esc(href) + "</p>";
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

	/* ---------------- auto-search on zoeken?q= URLs ----------------
	 * The search results live at a REAL URL (history/back works):
	 * when a page whose path contains "zoeken" carries ?q=, fetch and
	 * render the results into this page after it settles. */
	(function () {
		try {
			if (!globalThis.location || !globalThis.setTimeout) return;
			var host = globalThis.location.hostname || "";
			if (!/(^|\.)belastingdienst\.nl$/.test(host)) return;
			/* kpn.com etc. also use /zoeken paths - NEVER run the
			 * vinden fallback outside belastingdienst.nl */
			var p = globalThis.location.pathname || "";
			if (p.indexOf("zoeken") < 0) return;
			var sp = new URLSearchParams(globalThis.location.search || "");
			if (!sp.get("q")) return;
			globalThis.__qjsAutoSearchDone = false;
			globalThis.setTimeout(function () {
				if (globalThis.__qjsAutoSearchDone) return;
				globalThis.__qjsAutoSearchDone = true;
				globalThis.__qjsFormRaw = sp.toString();
				try { globalThis.__qjsFallbackSearch(); }
				catch (e) {
					try {
						(d || globalThis.document).write(
							"<p>QJS-SEARCH-ERR " + e + "</p>");
					} catch (e2) {}
				}
			}, 3000);
		} catch (e) {}
	})();

	/* ---------------- challenge-script API surface (Cloudflare etc.) ---- */
	(function () {
		function illegal(name) {
			var f = function () { throw new TypeError("Illegal constructor: " + name); };
			f.prototype = Object.create(null);
			return f;
		}
		var classes = ["HTMLElement", "HTMLScriptElement", "HTMLInputElement",
			"HTMLFormElement", "HTMLIFrameElement", "HTMLImageElement",
			"HTMLButtonElement", "HTMLTextAreaElement", "HTMLAnchorElement",
			"HTMLDivElement", "HTMLSpanElement", "HTMLCanvasElement",
			"HTMLBodyElement", "HTMLHeadElement", "HTMLLinkElement",
			"HTMLStyleElement", "HTMLMetaElement", "HTMLTitleElement",
			"HTMLParagraphElement", "HTMLUnknownElement", "HTMLTemplateElement", "HTMLPictureElement",
			"HTMLFieldSetElement", "HTMLLabelElement", "HTMLQuoteElement",
			"HTMLBRElement", "HTMLHRElement", "HTMLPreElement", "HTMLNavElement",
			"HTMLOListElement", "HTMLLIElement", "HTMLMapElement", "HTMLAreaElement",
			"HTMLProgressElement", "HTMLMeterElement", "HTMLDataListElement",
			"HTMLOutputElement", "HTMLDetailsElement", "HTMLSummaryElement",
			"HTMLDialogElement", "HTMLSlotElement", "HTMLEmbedElement",
			"HTMLObjectElement", "HTMLVideoElement", "HTMLAudioElement",
			"HTMLSourceElement", "HTMLTrackElement", "HTMLMarqueeElement", "HTMLOptionElement",
			"HTMLSelectElement", "HTMLTableElement", "HTMLUListElement",
			"SVGSVGElement", "SVGElement", "HTMLCollection", "NodeList",
			"NamedNodeMap", "DOMTokenList", "Screen", "History", "Location",
			"Storage", "XMLHttpRequest", "Image", "Option", "FormData",
			"FileReader", "Blob", "File", "Text", "CSSStyleDeclaration",
			"MediaQueryList", "Notification", "WebSocket", "Worker",
			"AbortController", "AbortSignal", "ReadableStream",
			"WritableStream", "TransformStream", "TextEncoder", "TextDecoder"];
		for (var i = 0; i < classes.length; i++) {
			if (!globalThis[classes[i]])
				globalThis[classes[i]] = illegal(classes[i]);
		}
	})();

	/* window.crypto: Turnstile reads getRandomValues */
	if (!globalThis.crypto) {
		globalThis.crypto = {
			getRandomValues: function (arr) {
				for (var i = 0; i < arr.length; i++)
					arr[i] = Math.floor(Math.random() * 256);
				return arr;
			},
			randomUUID: function () {
				return "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".replace(/[xy]/g, function (c) {
					var r = Math.random() * 16 | 0;
					return (c === "x" ? r : (r & 0x3 | 0x8)).toString(16);
				});
			},
			subtle: {}
		};
	}

	/* performance timing */
	if (!globalThis.performance || !globalThis.performance.now) {
		var __perf0 = Date.now();
		globalThis.performance = {
			now: function () { return Date.now() - __perf0; },
			timeOrigin: __perf0,
			mark: function () {}, measure: function () {},
			getEntriesByType: function () { return []; },
			getEntries: function () { return []; }
		};
	}

	/* navigator extras probed by bot detection */
	if (globalThis.navigator) {
		var nav = globalThis.navigator;
		if (nav.webdriver === undefined) nav.webdriver = false;
		if (nav.plugins === undefined) nav.plugins = { length: 0, item: function () { return null; } };
		if (nav.mimeTypes === undefined) nav.mimeTypes = { length: 0 };
		if (nav.languages === undefined) nav.languages = ["nl", "en"];
		if (nav.hardwareConcurrency === undefined) nav.hardwareConcurrency = 1;
		if (nav.maxTouchPoints === undefined) nav.maxTouchPoints = 0;
		if (nav.deviceMemory === undefined) nav.deviceMemory = 64;
		if (nav.vendor === undefined) nav.vendor = "";
		if (nav.product === undefined) nav.product = "Gecko";
		if (nav.sendBeacon === undefined) nav.sendBeacon = function () { return true; };
		if (nav.connection === undefined) nav.connection = { effectiveType: "3g", downlink: 1, rtt: 300 };
		if (nav.getBattery === undefined) nav.getBattery = function () { return Promise.resolve({ charging: false, level: 1 }); };
	}

	/* document.currentScript (Turnstile reads .dataset on it) */
	if (d && d.currentScript === undefined) {
		d.currentScript = elem();
		d.currentScript.dataset = {};
	}
	if (d && d.scripts === undefined) d.scripts = [];
	if (d && d.contentType === undefined) d.contentType = "text/html";
	if (d && d.compatMode === undefined) d.compatMode = "CSS1Compat";
	if (d && d.activeElement === undefined) d.activeElement = d.body || elem();

	/* ---------------- property-access tracer ----------------
	 * Proxy-wrap document, navigator and the window BINDING: every
	 * property the page reads is logged (once) via __qjsTraceLog to
	 * C:\JSTRACE.LOG - reveals what challenge scripts probe. */
	(function () {
		if (!globalThis.Proxy || !globalThis.__qjsTraceLog) return;
		var seen = {};
		function T(name, obj) {
			try {
				return new Proxy(obj, {
					get: function (t, k) {
						var key = name + "." + String(k);
						if (!seen[key]) { seen[key] = 1; globalThis.__qjsTraceLog(key); }
						return t[k];
					}
				});
			} catch (e) { return obj; }
		}
		try { globalThis.document = T("document", globalThis.document); } catch (e) {}
		try { globalThis.navigator = T("navigator", globalThis.navigator); } catch (e) {}
		try { globalThis.window = T("window", globalThis); } catch (e) {}
	})();

	/* ================= DOM->LINKS RENDER BRIDGE =================
	 * A real (small) DOM tree in JS: SPA frameworks (React etc.) build
	 * their document via createElement/appendChild/textContent. When
	 * such a tree actually gets content under <body>, we serialize it
	 * and re-render the whole page (__qjsReplacePage) so the content
	 * becomes VISIBLE in Links. Sites that only probe (jQuery) never
	 * build a tree -> page untouched. */
	(function () {
		var __dirty = false, __renderTimer = null, __lastSer = null;

		function DomText(t) {
			this.nodeType = 3;
			this.data = t || "";
			this.parentNode = null;
			this.childNodes = [];
		}
		DomText.prototype.cloneNode = function () { return new DomText(this.data); };
		Object.defineProperty(DomText.prototype, "textContent", {
			get: function () { return this.data; },
			set: function (v) { this.data = String(v); markDirty(); },
			configurable: true
		});
		DomText.prototype.appendChild = function () {};
		DomText.prototype.removeChild = function () {};

		function DomNode(tag) {
			this.nodeType = 1;
			this.tagName = String(tag || "div").toUpperCase();
			this.childNodes = [];
			this.parentNode = null;
			this.attributes = {};
			this.style = {};
			this._listeners = [];
			this._value = "";
			this._innerHTML = "";
			this._text = "";
			this.ownerDocument = globalThis.document;
		}
		DomNode.prototype.appendChild = function (n) {
			if (n && n.nodeType) {
				if (n.parentNode) n.parentNode.removeChild(n);
				n.parentNode = this;
				this.childNodes.push(n);
				markDirty();
			}
			return n;
		};
		DomNode.prototype.insertBefore = function (n, ref) {
			if (!n || !n.nodeType) return n;
			var i = ref ? this.childNodes.indexOf(ref) : -1;
			if (n.parentNode) n.parentNode.removeChild(n);
			n.parentNode = this;
			if (i < 0) this.childNodes.push(n);
			else this.childNodes.splice(i, 0, n);
			markDirty();
			return n;
		};
		DomNode.prototype.removeChild = function (n) {
			var i = this.childNodes.indexOf(n);
			if (i >= 0) {
				this.childNodes.splice(i, 1);
				n.parentNode = null;
				markDirty();
			}
			return n;
		};
		DomNode.prototype.replaceChild = function (n, o) {
			this.insertBefore(n, o);
			if (o) this.removeChild(o);
			return o;
		};
		DomNode.prototype.append = function () {
			for (var i = 0; i < arguments.length; i++) {
				var a = arguments[i];
				if (a && a.nodeType) this.appendChild(a);
				else this.appendChild(new DomText(String(a)));
			}
		};
		DomNode.prototype.prepend = DomNode.prototype.append;
		DomNode.prototype.replaceChildren = function () {
			while (this.firstChild) this.removeChild(this.firstChild);
			this.append.apply(this, arguments);
		};
		DomNode.prototype.cloneNode = function (deep) {
			var c = new DomNode(this.tagName);
			c.attributes = JSON.parse(JSON.stringify(this.attributes || {}));
			c._value = this._value; c._text = this._text;
			c._innerHTML = this._innerHTML;
			if (deep) {
				for (var i = 0; i < this.childNodes.length; i++)
					c.appendChild(this.childNodes[i].cloneNode(true));
			}
			return c;
		};
		DomNode.prototype.setAttribute = function (k, v) {
			this.attributes[k] = String(v);
			markDirty();
		};
		DomNode.prototype.getAttribute = function (k) {
			return (k in this.attributes) ? this.attributes[k] : null;
		};
		DomNode.prototype.removeAttribute = function (k) { delete this.attributes[k]; markDirty(); };
		DomNode.prototype.hasAttribute = function (k) { return k in this.attributes; };
		DomNode.prototype.addEventListener = function (t, fn) {
			if (typeof fn === "function") {
				this._listeners.push(fn);
				if (globalThis.__qjsAddEventListener) globalThis.__qjsAddEventListener(t, fn);
			}
		};
		DomNode.prototype.removeEventListener = function () {};
		DomNode.prototype.getElementsByTagName = function (t) {
			var out = [], tt = String(t).toUpperCase(), i;
			for (i = 0; i < this.childNodes.length; i++) {
				var c = this.childNodes[i];
				if (c.nodeType === 1) {
					if (tt === "*" || c.tagName === tt) out.push(c);
					out = out.concat(c.getElementsByTagName(t));
				}
			}
			return out;
		};
		DomNode.prototype.getElementById = function (id) {
			var i, c;
			for (i = 0; i < this.childNodes.length; i++) {
				c = this.childNodes[i];
				if (c.nodeType === 1) {
					if (c.attributes && c.attributes.id === id) return c;
					var r = c.getElementById(id);
					if (r) return r;
				}
			}
			return null;
		};
		DomNode.prototype.contains = function (n) {
			while (n) { if (n === this) return true; n = n.parentNode; }
			return false;
		};
		DomNode.prototype.closest = function () { return null; };
		DomNode.prototype.matches = function () { return false; };
		DomNode.prototype.focus = function () {};
		DomNode.prototype.blur = function () {};
		DomNode.prototype.click = function () {
			var l = this._listeners.slice(), i;
			for (i = 0; i < l.length; i++) { try { l[i]({type:"click",target:this}); } catch (e) {} }
		};
		DomNode.prototype.getContext = function () { return {}; };
		DomNode.prototype.toDataURL = function () { return "data:,"; };
		DomNode.prototype.getBoundingClientRect = function () {
			return { top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0, x: 0, y: 0 };
		};
		DomNode.prototype.scrollIntoView = function () {};
		DomNode.prototype.insertAdjacentHTML = function () { markDirty(); };
		DomNode.prototype.dispatchEvent = function () { return true; };
		DomNode.prototype.querySelector = function (sel) {
			var all = __qjsQueryAll(this, sel);
			return all.length ? all[0] : null;
		};
		DomNode.prototype.querySelectorAll = function (sel) {
			return __qjsQueryAll(this, sel);
		};

		/* property accessors via defineProperty */
		(function () {
			var proto = DomNode.prototype;
			Object.defineProperty(proto, "firstChild", {
				get: function () { return this.childNodes[0] || null; },
				configurable: true
			});
			Object.defineProperty(proto, "lastChild", {
				get: function () { return this.childNodes[this.childNodes.length - 1] || null; },
				configurable: true
			});
			Object.defineProperty(proto, "nextSibling", {
				get: function () {
					if (!this.parentNode) return null;
					var i = this.parentNode.childNodes.indexOf(this);
					return this.parentNode.childNodes[i + 1] || null;
				},
				configurable: true
			});
			Object.defineProperty(proto, "previousSibling", {
				get: function () {
					if (!this.parentNode) return null;
					var i = this.parentNode.childNodes.indexOf(this);
					return i > 0 ? this.parentNode.childNodes[i - 1] : null;
				},
				configurable: true
			});
			Object.defineProperty(proto, "children", {
				get: function () {
					return this.childNodes.filter(function (c) { return c.nodeType === 1; });
				},
				configurable: true
			});
			Object.defineProperty(proto, "childElementCount", {
				get: function () { return this.children.length; },
				configurable: true
			});
			Object.defineProperty(proto, "id", {
				get: function () { return this.attributes.id || ""; },
				set: function (v) { this.attributes.id = String(v); markDirty(); },
				configurable: true
			});
			Object.defineProperty(proto, "className", {
				get: function () { return this.attributes["class"] || ""; },
				set: function (v) { this.attributes["class"] = String(v); markDirty(); },
				configurable: true
			});
			Object.defineProperty(proto, "value", {
				get: function () { return this._value; },
				set: function (v) { this._value = String(v); markDirty(); },
				configurable: true
			});
			Object.defineProperty(proto, "dataset", {
				get: function () {
					var ds = {}, k;
					for (k in this.attributes)
						if (k.indexOf("data-") === 0)
							ds[k.slice(5).replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); })] = this.attributes[k];
					return ds;
				},
				configurable: true
			});
			Object.defineProperty(proto, "classList", {
				get: function () {
					var self = this;
					var list = (self.attributes["class"] || "").split(/\s+/).filter(Boolean);
					return {
						add: function (c) { if (list.indexOf(c) < 0) list.push(c); self.attributes["class"] = list.join(" "); markDirty(); },
						remove: function (c) { list = list.filter(function (x) { return x !== c; }); self.attributes["class"] = list.join(" "); markDirty(); },
						toggle: function (c) { this.add(c); },
						contains: function (c) { return list.indexOf(c) >= 0; }
					};
				},
				configurable: true
			});
			Object.defineProperty(proto, "textContent", {
				get: function () {
					var s = "", i;
					for (i = 0; i < this.childNodes.length; i++)
						s += (this.childNodes[i].nodeType === 3) ? this.childNodes[i].data
							: (this.childNodes[i].textContent || "");
					return s;
				},
				set: function (v) {
					while (this.firstChild) this.removeChild(this.firstChild);
					this.appendChild(new DomText(String(v)));
				},
				configurable: true
			});
			Object.defineProperty(proto, "innerText", {
				get: function () { return this.textContent; },
				set: function (v) { this.textContent = v; },
				configurable: true
			});
			Object.defineProperty(proto, "innerHTML", {
				get: function () { return this._innerHTML; },
				set: function (v) {
					this._innerHTML = String(v);
					while (this.firstChild) this.removeChild(this.firstChild);
					try { parseHTMLInto(String(v), this); } catch (e) {}
					markDirty();
				},
				configurable: true
			});
		})();

		/* tiny HTML parser for innerHTML values */
		function parseHTMLInto(html, parent) {
			var stack = [parent], pos = 0, m;
			var tagRe = /<(\/)?([a-zA-Z][a-zA-Z0-9]*)((?:[^>"']|"[^"]*"|'[^']*')*?)(\/?)>/g;
			var esc = function (s) { return s; }; /* text kept raw */
			while ((m = tagRe.exec(html)) !== null) {
				if (m.index > pos) {
					var txt = html.slice(pos, m.index).replace(/&nbsp;/g, " ").replace(/&amp;/g, "&");
					if (txt.trim()) stack[stack.length - 1].appendChild(new DomText(txt));
				}
				var closing = m[1] === "/", name = m[2].toUpperCase(), selfc = m[4] === "/";
				if (closing) {
					for (var k = stack.length - 1; k > 0; k--)
						if (stack[k].tagName === name) { stack.length = k; break; }
				} else if (!selfc && name !== "BR" && name !== "HR" && name !== "IMG" && name !== "INPUT" && name !== "META" && name !== "LINK") {
					var n = new DomNode(name);
					var attrRe = /([a-zA-Z_:][-a-zA-Z0-9_:.]*)\s*=\s*("([^"]*)"|'([^']*)'|[^\s>]+)/g;
					var am, attrs = m[3] || "";
					while ((am = attrRe.exec(attrs)) !== null)
						n.attributes[am[1]] = (am[3] !== undefined ? am[3] : (am[4] !== undefined ? am[4] : am[2]));
					stack[stack.length - 1].appendChild(n);
					stack.push(n);
				} else {
					var sn = new DomNode(name);
					stack[stack.length - 1].appendChild(sn);
				}
				pos = tagRe.lastIndex;
			}
			if (pos < html.length) {
				var tail = html.slice(pos).replace(/&nbsp;/g, " ").replace(/&amp;/g, "&");
				if (tail.trim()) stack[stack.length - 1].appendChild(new DomText(tail));
			}
		}

		/* very small selector engine: "tag", "#id", ".class", "tag.cls" */
		function __qjsQueryAll(root, sel) {
			var out = [];
			if (!sel) return out;
			sel = String(sel).split(",")[0].trim();
			var m = /^(\w+)?(?:#([\w-]+))?(?:\.([\w-]+))?$/.exec(sel);
			function walk(n) {
				var i, c;
				for (i = 0; i < n.childNodes.length; i++) {
					c = n.childNodes[i];
					if (c.nodeType === 1) {
						var ok = true;
						if (m) {
							if (m[1] && c.tagName !== m[1].toUpperCase()) ok = false;
							if (ok && m[2] && c.attributes.id !== m[2]) ok = false;
							if (ok && m[3] && (" " + (c.attributes["class"] || "") + " ").indexOf(" " + m[3] + " ") < 0) ok = false;
						} else ok = false;
						if (ok) out.push(c);
						walk(c);
					}
				}
			}
			walk(root);
			return out;
		}

		/* serializer: tree -> HTML for Links */
		function escText(s) {
			return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
		}
		function escAttr(s) {
			return escText(s).replace(/"/g, "&quot;");
		}
		function serialize(n) {
			if (n.nodeType === 3) return escText(n.data);
			if (n.nodeType !== 1) return "";
			var t = n.tagName, h = "<" + t, k;
			for (k in n.attributes)
				if (k === "id" || k === "class" || k === "href" || k === "src" || k === "type" || k === "name")
					h += " " + k + '="' + escAttr(n.attributes[k]) + '"';
			h += ">";
			if (t === "SCRIPT" || t === "STYLE" || t === "TEMPLATE") return h + "</" + t + ">";
			if (t === "INPUT") {
				var ph = n.attributes.placeholder || n._value || "";
				if (ph) h += escText(ph);
				return h + "</" + t + ">";
			}
			var i;
			for (i = 0; i < n.childNodes.length; i++)
				h += serialize(n.childNodes[i]);
			h += "</" + t + ">";
			return h;
		}

		function markDirty() {
			__dirty = true;
			if (__renderTimer || !globalThis.setTimeout) return;
			__renderTimer = true;
			globalThis.setTimeout(function () {
				__renderTimer = false;
				if (!__dirty) return;
				__dirty = false;
				var body = globalThis.__qjsDomBody;
				if (!body) return;
				var ser = serialize(body);
				/* SPA roots: page-static containers (e.g. <div id=root>)
				 * that scripts populated - they hang outside our body
				 * tree, so serialize them too */
				var tn = globalThis.__qjsTrackedNodes || [], k;
				for (k = 0; k < tn.length; k++) {
					var n = tn[k];
					if (n.childElementCount === 0) continue;
					var anc = n, inBody = false;
					while (anc) { if (anc === body) { inBody = true; break; } anc = anc.parentNode; }
					if (!inBody) ser += serialize(n);
				}
				if (!body.childElementCount && ser.length === 0) return;
				/* only render SUBSTANTIAL trees: jQuery-driven sites
				 * append stray probe nodes to body all the time -
				 * replacing a full page with those blanks it */
				if (!ser || ser.length < 600 || ser === __lastSer) return;
				if (globalThis.__qjsTraceLog)
					globalThis.__qjsTraceLog("DOM-RENDER len=" + ser.length);
				__lastSer = ser;
				var doc = globalThis.document;
				if (doc && doc.__qjsReplacePage) {
					doc.__qjsReplacePage(
						"<h1>" + escText((globalThis.document && globalThis.document.title) || "") + "</h1>" + ser);
				}
			}, 700);
		}

		/* body/head/documentElement become real nodes; keep permissive
		 * page-source removal for ids that are NOT in the JS tree */
		var __body = new DomNode("body");
		var __head = new DomNode("head");
		var __docel = new DomNode("html");
		__docel.appendChild(__head);
		__docel.appendChild(__body);
		globalThis.__qjsDomBody = __body;
		globalThis.__qjsDomHead = __head;
		globalThis.__qjsDomDocel = __docel;
		globalThis.__qjsDomNode = DomNode;
		globalThis.__qjsDomText = DomText;
		globalThis.__qjsParseHTMLInto = parseHTMLInto;

		/* re-wire document (this block runs AFTER the document setup
		 * above, which had to fall back to the permissive stubs) */
		if (globalThis.document) {
			var dd = globalThis.document;
			dd.createElement = function (t) { return new DomNode(t); };
			dd.createDocumentFragment = function () { return new DomNode("#fragment"); };
			dd.createTextNode = function (t) { return new (globalThis.__qjsDomText)(t); };
			dd.body = __body;
			dd.head = __head;
			dd.documentElement = __docel;
			dd.getElementById = function (id) {
				var hit = __body.getElementById(id);
				if (hit) return hit;
				return trackedElem(id);
			};
			dd.querySelector = function (sel) {
				var hit = null;
				try { hit = __body.querySelector(sel); } catch (e) {}
				if (hit) return hit;
				var m = /^#([A-Za-z0-9_-]+)$/.exec(sel || "");
				return trackedElem(m ? m[1] : null);
			};
			dd.querySelectorAll = function (sel) {
				var r;
				try { r = __body.querySelectorAll(sel); } catch (e) { r = []; }
				return (r && r.length) ? r : [];
			};
			dd.getElementsByTagName = function (t) {
				var r = __body.getElementsByTagName(t);
				return (r && r.length) ? r : [];
			};
		}
	})();


	/* React scheduler primitives */
	if (!globalThis.MessageChannel) {
		globalThis.MessageChannel = function () {
			var self = this;
			this.port1 = {
				postMessage: function (d) { globalThis.setTimeout(function () { if (self.port1.onmessage) self.port1.onmessage({ data: d }); }, 0); }
			};
			this.port2 = {
				postMessage: function (d) { globalThis.setTimeout(function () { if (self.port2.onmessage) self.port2.onmessage({ data: d }); }, 0); }
			};
		};
	}
	if (!globalThis.queueMicrotask) {
		globalThis.queueMicrotask = function (f) { Promise.resolve().then(f); };
	}
})();

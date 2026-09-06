/* dom_bootstrap.js — evaluated in every QuickJS context after the C
 * register_globals(). Enriches the minimal C stubs into a "permissive
 * document" good enough for jQuery 3.6 + Bootstrap site bundles.
 * Verified against www.belastingdienst.nl's script chain (host QuickJS
 * harness /tmp/guest_prelude5.js). Keep everything no-op/absorbing. */
(function () {
	"use strict";

	function elem() {
		return {
			nodeType: 1, style: {}, className: "", innerHTML: "", textContent: "", value: "",
			checked: false,
			parentNode: { removeChild: function () {} },
			childNodes: [],
			appendChild: function (c) { this.childNodes = [c]; return c; },
			removeChild: function () {},
			get lastChild() { return this.childNodes[this.childNodes.length - 1] || elem(); },
			cloneNode: function () { var n = elem(); n.checked = this.checked; return n; },
			setAttribute: function () {}, getAttribute: function () { return null; },
			removeAttribute: function () {},
			addEventListener: function () {}, removeEventListener: function () {},
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
		d.querySelectorAll = function () { return []; };
		d.getElementsByTagName = function () { return []; };
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

	/* timers: no event loop yet — return ids, never fire */
	var __tid = 0;
	globalThis.setTimeout = function () { return ++__tid; };
	globalThis.clearTimeout = function () {};
	globalThis.setInterval = function () { return ++__tid; };
	globalThis.clearInterval = function () {};
	if (w) {
		w.setTimeout = globalThis.setTimeout;
		w.clearTimeout = globalThis.clearTimeout;
		w.setInterval = globalThis.setInterval;
		w.clearInterval = globalThis.clearInterval;
	}

	globalThis.getComputedStyle = globalThis.getComputedStyle ||
		function () { return { getPropertyValue: function () { return ""; } }; };
	globalThis.matchMedia = globalThis.matchMedia ||
		function () { return { matches: false, addListener: function () {} }; };
	globalThis.requestAnimationFrame = function () { return 0; };
	globalThis.cancelAnimationFrame = function () {};

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
				if (p[0]) m[decodeURIComponent(p[0])] = decodeURIComponent(p[1] || "");
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
})();

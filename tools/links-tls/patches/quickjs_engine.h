/*
 * quickjs_engine.h -- Cure B+ engine for Links 2.30: QuickJS (ES2020)
 * replacing MuJS. Same jsint-facing interface as mujs_engine.h.
 */
#ifndef QUICKJS_ENGINE_H
#define QUICKJS_ENGINE_H
#include "links.h"
#include "quickjs.h"

struct javascript_context {
	JSRuntime *rt;
	JSContext *ctx;
	void *ptr;                /* struct f_data_c* */
	long id;
	long js_id;
	unsigned char *cookies;
	int zaplatim;
	int dead;
	int logged_error;         /* JSERROR.LOG throttle */
};

typedef struct javascript_context js_context;

#endif

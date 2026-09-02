#ifndef MUJS_ENGINE_H
#define MUJS_ENGINE_H
/*
 * mujs_engine -- Cure B JavaScript engine for Links 2.30 (see
 * LINKS-JAVASCRIPT-REPORT.md): replaces the removed 2007 ipret/javascript
 * interpreter with MuJS 1.3.6 (ES5) while keeping links-2.30's maintained
 * jsint.c glue and its document.write machinery untouched.
 *
 * The interface is exactly what links.h declares and what jsint.c calls:
 *   js_create_context / js_destroy_context / js_execute_code
 * plus the async-dialog downcall entry points that jsint.c links against
 * (kept as no-ops because MuJS executes synchronously; dialogs are fired
 * deferred via install_timer like the old engine did).
 */
#include "links.h"
#include "mujs.h"

typedef struct javascript_context js_context;

struct javascript_context {
	js_State *J;              /* MuJS state for this document */
	void *ptr;                /* struct f_data_c* the scripts run in */
	long id;                  /* (fd->id<<JS_OBJ_MASK_SIZE)|JS_OBJ_T_DOCUMENT */
	long js_id;               /* unique id checked by jsint dialog callbacks */
	unsigned char *cookies;   /* document.cookie accumulator owned by jsint */
	int zaplatim;             /* "the form was submitted" flag read by jsint */
	int dead;                 /* guard against use after destroy */
};

#endif

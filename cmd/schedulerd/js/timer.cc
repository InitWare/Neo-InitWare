#include <cassert>

#include "../app/app.h"
#include "cutils.h"
#include "js2.h"
#include "quickjs.h"

JSClassID JSTimer::clsid;

void
JSTimer::app_cb(App::timerid_t id, uintptr_t udata)
{
	JSValue ret, func;

	assert(id = m_id);

	func = JS_DupValue(m_ctx, m_func);
	ret = JS_Call(m_ctx, func, JS_UNDEFINED, m_args.size(),
	    (JSValueConst *)m_args.data());
	JS_FreeValue(m_ctx, func);
	if (JS_IsException(ret))
		JS::log_exception(m_ctx);
	JS_FreeValue(m_ctx, ret);
}

void
JSTimer::clear()
{
	if (m_id != 0)
		JS::from_context(m_ctx)->m_app->del_timer(m_id);
	JS_UnRef(m_ctx, &m_func);
	for (auto arg : m_args)
		JS_FreeValue(m_ctx, arg);
	m_args.clear();
	JS_UnRef(m_ctx, &m_obj);
}

void
JSTimer::finalizer()
{
	clear();
}

void
JSTimer::gc_mark(JSRuntime *rt, JS_MarkFunc *mark_func)
{
	JS_MarkValue(rt, m_func, mark_func);
	for (auto &arg : m_args)
		JS_MarkValue(rt, arg, mark_func);
}

JSValue
JSTimer::setTimeout(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv, int recurs)
{
	JS *js = JS::from_context(ctx);
	JSValue obj, func;
	int64_t ms;
	JSTimer *timer;
	App::timerid_t id;

	func = argv[0];
	if (!JS_IsFunction(ctx, func))
		return JS_ThrowTypeError(ctx, "Not a function.");
	if (JS_ToInt64(ctx, &ms, argv[1]))
		return JS_EXCEPTION;

	argc -= 2;
	argv += 2;

	obj = JS_NewObjectClass(ctx, clsid);
	if (JS_IsException(obj))
		return obj;

	timer = new JSTimer;
	if (!timer)
		return JS_ThrowOutOfMemory(ctx);

	id = js->m_app->add_timer(recurs, ms,
	    std::bind(&JSTimer::app_cb, timer, std::placeholders::_1,
		std::placeholders::_2));
	if (id == 0) {
		delete timer;
		JS_FreeValue(ctx, obj);
		return JS_ThrowInternalError(ctx, "Failed to add error");
	}

	timer->m_ctx = ctx;
	timer->m_id = id;
	timer->m_func = JS_DupValue(ctx, func);
	timer->m_obj = JS_DupValue(ctx, obj);
	timer->m_args = argv_to_vec(ctx, argc, argv);

	JS_SetOpaque(obj, timer);

	return obj;
}

JSValue
JSTimer::clearTimeout(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	clear();
	return JS_UNDEFINED;
}

JSClassDef JSTimer::cls = {
	"Timer",
	.finalizer = JSTimer::finalizer_s,
	.gc_mark = JSTimer::gc_mark_s,
};

JSCFunctionListEntry JSTimer::funcs[] = {
	JS_CFUNC_MAGIC_DEF("setTimeout", 2, JSTimer::setTimeout, false),
	JS_CFUNC_MAGIC_DEF("setInterval", 2, JSTimer::setTimeout, true),
	JS_CFUNC_DEF("clearTimeout", 1,
	    (&first_arg_fun<JSTimer, &JSTimer::clearTimeout>)),
	JS_CFUNC_DEF("clearInterval", 1,
	    (&first_arg_fun<JSTimer, &JSTimer::clearTimeout>)),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Timer",
	    JS_PROP_CONFIGURABLE),
};

size_t JSTimer::nfuncs = countof(JSTimer::funcs);
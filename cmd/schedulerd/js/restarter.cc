#include "quickjs.h"
#include "restarter.h"

JSValue
JSRestarter::create(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	JS *js = JS::from_context(ctx);
	JSValue obj, fnStart, fnStop;
	JSRestarter *restarter;

	obj = JS_NewObjectClass(ctx, clsid);
	if (JS_IsException(obj))
		return obj;

	restarter = new JSRestarter(obj, js->m_app->m_sched);
	if (!restarter)
		return JS_ThrowOutOfMemory(ctx);

	restarter->m_ctx = ctx;
	restarter->m_delegate = JS_DupValue(ctx, argv[0]);

	JS_SetOpaque(obj, restarter);

	return obj;
}

JSValue
JSRestarter::setForType(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	JS *js = JS::from_context(ctx);
	const char *type;

	type = JS_ToCString(m_ctx, argv[1]);
	if (type == NULL)
		return JS_ThrowTypeError(m_ctx,
		    "Second argument isn't a string");

	js->m_app->restarters[type] = this;

	return JS_UNDEFINED;
}

bool
JSRestarter::start(Transaction::Job::Id obj)
{
	JSValue ret;
	JSAtom fun_name = JS_NewAtom(m_ctx, "startObject");
	JSValue args[3];

	args[0] = JS_NewInt64(m_ctx, obj);

	ret = JS_Invoke(m_ctx, m_delegate, fun_name, 1, args);
	if (JS_IsException(ret))
		JS::log_exception(m_ctx);
	JS_FreeValue(m_ctx, args[0]);
	JS_FreeValue(m_ctx, ret);

	return false;
}

bool
JSRestarter::stop(Transaction::Job::Id obj)
{
	return false;
}

void
JSRestarter::gc_mark(JSRuntime *rt, JS_MarkFunc *mark_func)
{
	JS_MarkValue(rt, m_delegate, mark_func);
}

void
JSRestarter::finalizer()
{
}

JSClassID JSRestarter::clsid;

JSClassDef JSRestarter::cls = {
	"Restarter",
	.finalizer = JSRestarter::finalizer_s,
	.gc_mark = JSRestarter::gc_mark_s,
};

JSCFunctionListEntry JSRestarter::funcs[] = {
	JS_CFUNC_DEF("createRestarter", 1, JSRestarter::create),
	JS_CFUNC_DEF("setRestarterForType", 2,
	    (&first_arg_fun<JSRestarter, &JSRestarter::setForType>)),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Restarter",
	    JS_PROP_CONFIGURABLE),
};

size_t JSRestarter::nfuncs = countof(JSRestarter::funcs);
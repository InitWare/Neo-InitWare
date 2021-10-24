#include "js2.h"

JSValue
JSPromiseDesc::init(JSContext *ctx)
{
	JSValue fnResolve[2];

	m_promObj = JS_NewPromiseCapability(ctx, fnResolve);
	if (JS_IsException(m_promObj))
		return JS_EXCEPTION;

	m_fnResolve[0] = JS_DupValue(ctx, fnResolve[0]);
	m_fnResolve[1] = JS_DupValue(ctx, fnResolve[1]);

	return JS_DupValue(ctx, m_promObj);
}

void
JSPromiseDesc::clear()
{
	JS_UnRef(m_ctx, &m_promObj);
	JS_UnRef(m_ctx, &m_fnResolve[0]);
	JS_UnRef(m_ctx, &m_fnResolve[1]);
}

JSPromiseDesc::~JSPromiseDesc()
{
	clear();
}

void
JSPromiseDesc::settle(JSContext *ctx, bool reject, int argc, JSValueConst *argv)
{
	JSValue ret = JS_Call(ctx, m_fnResolve[reject ? 1 : 0], JS_UNDEFINED,
	    argc, argv);

	for (int i = 0; i < argc; i++)
		JS_FreeValue(ctx, argv[i]);
	JS_FreeValue(ctx, ret);
	clear();
}

void
JSPromiseDesc::resolve(JSContext *ctx, int argc, JSValueConst *argv)
{
	settle(ctx, false, argc, argv);
}

void
JSPromiseDesc::reject(JSContext *ctx, int argc, JSValueConst *argv)
{
	settle(ctx, true, argc, argv);
}

void
JSPromiseDesc::gc_mark(JSRuntime *rt, JS_MarkFunc *mark_func)
{
	JS_MarkValue(rt, m_promObj, mark_func);
	JS_MarkValue(rt, m_fnResolve[0], mark_func);
	JS_MarkValue(rt, m_fnResolve[1], mark_func);
}
#include <iostream>

#include "../app/app.h"
#include "js.h"
#include "qjspp.h"

struct JSTimer {
	qjs::Value js_callback;
	App::timerid_t m_id;

	static JSTimer *setTimeout(qjs::Value function, int64_t ms);
	static JSTimer *setInterval(qjs::Value function, int64_t ms);
	static void clearTimeout(JSTimer *timer);

	JSTimer(qjs::Value function, int64_t ms, bool recurs = false);

	void app_cb(App::timerid_t id, uintptr_t udata);
};

struct JSPromise {
	qjs::Value fnOnFulfilled, fnOnRejected, promCapability;

	static std::unique_ptr<JSPromise> create(qjs::Context &ctx);

	JSPromise(qjs::Value &&promCapability, qjs::Value &&onFulfilled,
	    qjs::Value &&onRejected)
	    : fnOnFulfilled(std::move(onFulfilled))
	    , fnOnRejected(std::move(onRejected))
	    , promCapability(std::move(promCapability)) {};

	void resolve(qjs::Value arg);
	void reject(qjs::Value reason);
};

void setup_fs(qjs::Context *ctx);
void setup_restarter(qjs::Context *ctx);
void setup_sched(JS &js);

JSTimer::JSTimer(qjs::Value function, int64_t ms, bool recurs)
    : js_callback(function)
{
	m_id = JS::from_ctx(function.ctx)
		   .m_app.add_timer(recurs, ms,
		       std::bind(&JSTimer::app_cb, this, std::placeholders::_1,
			   std::placeholders::_2));
}

JSTimer *
JSTimer::setTimeout(qjs::Value function, int64_t ms)
{
	return new JSTimer(function, ms);
}

JSTimer *
JSTimer::setInterval(qjs::Value function, int64_t ms)
{
	return new JSTimer(function, ms, true);
}

void
JSTimer::clearTimeout(JSTimer *timer)
{
	JS::from_ctx(timer->js_callback.ctx).m_app.del_timer(timer->m_id);
	delete timer;
}

void
JSTimer::app_cb(App::timerid_t id, uintptr_t udata)
{
	js_callback.as<std::function<void()>>()();
}

std::unique_ptr<JSPromise>
JSPromise::create(qjs::Context &ctx)
{
	JSValue fnResolve[2];
	JSValue promObj = JS_NewPromiseCapability(ctx.ctx, fnResolve);
	return std::make_unique<JSPromise>(qjs::Value(ctx.ctx, promObj),
	    qjs::Value(ctx.ctx, fnResolve[0]),
	    qjs::Value(ctx.ctx, fnResolve[1]));
}

void
JSPromise::resolve(qjs::Value arg)
{
	fnOnFulfilled.as<std::function<void(qjs::Value &)>>()(arg);
}

void
JSPromise::reject(qjs::Value reason)
{
	fnOnRejected.as<std::function<void(qjs::Value &)>>()(reason);
}

JS::JS(App &app)
    : m_app(app)
{
	rt = new qjs::Runtime;
	ctx = new qjs::Context(*rt);

	JS_SetRuntimeOpaque(rt->rt, this);
	JS_SetModuleLoaderFunc(rt->rt, NULL, js_module_loader, NULL);

	js_std_add_helpers(ctx->ctx, 0, NULL);
	setup_fs(ctx);
	setup_restarter(ctx);
	setup_sched(*this);

	qjs::Context::Module &mod = ctx->addModule("@iw/timer");
	mod.class_<JSTimer>("Timer").fun<&JSTimer::js_callback>("callback");
	mod.function<JSTimer::setTimeout>("setTimeout")
	    .function<JSTimer::setInterval>("setInterval")
	    .function<JSTimer::clearTimeout>("clearTimeout")
	    .function<JSTimer::clearTimeout>("clearInterval");
}

void
JS::run_pending_jobs()
{
	int ret;

	for (;;) {
		JSContext *cctx;
		ret = JS_ExecutePendingJob(rt->rt, &cctx);

		if (ret < 0)
			qjs::Value val = { cctx, JS_GetException(cctx) };
		else if (ret == 0)
			break;
	}
}

JS &
JS::from_ctx(JSContext *ctx)
{
	return *(JS *)JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
}

void
JS::log_exception(qjs::Context *ctx)
{
	qjs::Value val = ctx->getException();
	qjs::Value stack = val["stack"];
	std::cout << val.as<std::string>() << "\n"
		  << stack.as<std::string>() << "\n";
}

#if 0
#include "fs.h"
#include "js2.h"
#include "quickjs-libc.h"
#include "restarter.h"

std::vector<JSValue>
argv_to_vec(JSContext *ctx, int argc, JSValueConst argv[])
{
	std::vector<JSValue> vec;
	for (int i = 0; i < argc; i++)
		vec.push_back(JS_DupValue(ctx, argv[i]));
	return vec;
}

void
JS_UnRef(JSContext *ctx, JSValue *val)
{
	JS_FreeValue(ctx, *val);
	*val = JS_UNDEFINED;
}

JS::JS(App *app)
    : m_app(app)
{
	JSModuleDef *mod;

	m_rt = JS_NewRuntime();
	m_ctx = JS_NewContext(m_rt);

	JS_SetRuntimeOpaque(m_rt, this);
	JS_SetContextOpaque(m_ctx, this);
	JS_SetModuleLoaderFunc(m_rt, NULL, js_module_loader, NULL);

	mod = JS_NewCModule(m_ctx, "@initware", mod_init);
	JSTimer::mod_export(m_ctx, mod);
	JSFD::mod_export(m_ctx, mod);
	JSRestarter::mod_export(m_ctx, mod);
	JSFS::mod_export(m_ctx, mod);

	js_std_add_helpers(m_ctx, 0, NULL);
}

int
JS::mod_init(JSContext *ctx, JSModuleDef *mod)
{
	JSTimer::mod_init(ctx, mod);
	JSFD::mod_init(ctx, mod);
	JSRestarter::mod_init(ctx, mod);
	JSFS::mod_init(ctx, mod);

	return 0;
}

const char *
JS::tag_cstr(int64_t tag)
{
	switch (tag) {
	case JS_TAG_INT:
		return "int";
	case JS_TAG_BOOL:
		return "bool";
	case JS_TAG_NULL:
		return "null";
	case JS_TAG_UNDEFINED:
		return "undefined";
	case JS_TAG_CATCH_OFFSET:
		return "catch offset";
	case JS_TAG_EXCEPTION:
		return "exception";
	case JS_TAG_FLOAT64:
		return "float64";
	case JS_TAG_MODULE:
		return "module";
	case JS_TAG_OBJECT:
		return "object";
	case JS_TAG_STRING:
		return "string";
	case JS_TAG_FIRST:
		return "first";
	case JS_TAG_BIG_INT:
		return "big_int";
	case JS_TAG_BIG_FLOAT:
		return "big_float";
	case JS_TAG_SYMBOL:
		return "symbol";
	case JS_TAG_FUNCTION_BYTECODE:
		return "function bytecode";
	default:
		return "unknown type!";
	}
}

void
JS::log_error(JSContext *ctx, JSValueConst exc)
{
	print_obj(ctx, exc);

	if (JS_IsError(ctx, exc)) {
		JSValue val = JS_GetPropertyStr(ctx, exc, "stack");
		if (!JS_IsUndefined(val))
			print_obj(ctx, val);
		JS_FreeValue(ctx, val);
	}
}

void
JS::print_obj(JSContext *ctx, JSValueConst obj)
{
	const char *str = JS_ToCString(ctx, obj);

	if (!str) {
		log_err("[unknown]\n");
		return;
	}

	log_err("%s", str);
	JS_FreeCString(ctx, str);
}

void
JS::eval(JSContext *ctx, std::string &filename, std::string &txt,
    int eval_flags)
{
	JSValue res = JS_Eval(ctx, txt.c_str(), txt.length(), filename.c_str(),
	    eval_flags);

	if (JS_IsException(res))
		log_exception(ctx);

	JS_FreeValue(ctx, res);
}

void
JS::run_pending_jobs()
{
	JSContext *ctx;
	int ret;

	for (;;) {
		ret = JS_ExecutePendingJob(JS_GetRuntime(m_ctx), &ctx);
		if (ret < 0)
			log_exception(ctx);
		else if (ret == 0)
			break;
	}
}

#endif
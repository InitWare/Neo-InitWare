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

void
JS::loadObject(std::string name)
{
	try {
		ctx->global()["loadObject"]
		    .as<std::function<void(std::string)>>()(name);
	} catch (const qjs::exception &exc) {
		log_exception(ctx);
	}
}
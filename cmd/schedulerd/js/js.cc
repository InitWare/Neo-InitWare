
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

	mod = JS_NewCModule(m_ctx, "fs", JSFS::mod_init);
	JSFS::mod_export(m_ctx, mod);

	js_std_add_helpers(m_ctx, 0, NULL);
}

int
JS::mod_init(JSContext *ctx, JSModuleDef *mod)
{
	JSTimer::mod_init(ctx, mod);
	JSFD::mod_init(ctx, mod);
	JSRestarter::mod_init(ctx, mod);

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

JS *
JS::from_context(JSContext *ctx)
{
	return (JS *)JS_GetContextOpaque(ctx);
}

void
JS::log_exception(JSContext *ctx)
{
	JSValue exc = JS_GetException(ctx);
	log_error(ctx, exc);
	JS_FreeValue(ctx, exc);
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

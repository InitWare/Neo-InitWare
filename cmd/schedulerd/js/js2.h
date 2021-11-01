#ifndef JS2_H_
#define JS2_H_

#include "../app/app.h"
#include "cutils.h"
#include "js.h"

template <typename T> struct JSCXXClass {
	static void finalizer_s(JSRuntime *rt, JSValue val);
	static void gc_mark_s(JSRuntime *rt, JSValueConst val,
	    JS_MarkFunc *mark_func);

	static void mod_init(JSContext *ctx, JSModuleDef *mod);
	static void mod_export(JSContext *ctx, JSModuleDef *mod);
};

struct JSTimer : public JSCXXClass<JSTimer> {
	static JSClassID clsid;
	static JSClassDef cls;
	static JSCFunctionListEntry funcs[];
	static size_t nfuncs;

	JSContext *m_ctx;
	App::timerid_t m_id;
	JSValue m_obj;
	JSValue m_func;
	std::vector<JSValue> m_args;
	bool m_recurs; ///< whether this is a recurring interval timer

	/* factory methods */
	static JSValue setTimeout(JSContext *ctx, JSValueConst this_val,
	    int argc, JSValueConst *argv, int magic);

	/* instance methods */
	JSValue clearTimeout(JSContext *ctx, JSValueConst this_val, int argc,
	    JSValueConst *argv);

	/* instance callbacks */
	void app_cb(App::timerid_t id, uintptr_t udata);
	void clear();
	void finalizer();
	void gc_mark(JSRuntime *rt, JS_MarkFunc *mark_func);
};

/**
 * Promise descriptor - not a JS object itself, but is associated with one.
 */
class JSPromiseDesc {

    public:
	JSContext *m_ctx;
	JSValue m_promObj = JS_UNDEFINED; ///< associated JS promise object
	JSValue m_fnResolve[2] = {
		JS_UNDEFINED
	}; /* resolve and reject functions */

	/* C++ instance methods */
	~JSPromiseDesc();

	bool is_pending(); ///< whether there is an associated JS promise obj

	JSValue init(JSContext *ctx); ///< initialise
	void clear();		      ///< unset and unref associated JS objects
	/** Settle a promise. Frees argv. */
	void settle(JSContext *ctx, bool reject, int argc, JSValueConst *argv);
	void resolve(JSContext *ctx, int argc, JSValueConst *argv);
	void reject(JSContext *ctx, int argc, JSValueConst *argv);

	void gc_mark(JSRuntime *rt, JS_MarkFunc *mark_func);
};

struct JSFD : public JSCXXClass<JSFD> {
	static JSClassID clsid;
	static JSClassDef cls;
	static JSCFunctionListEntry funcs[];
	static size_t nfuncs;

	JSContext *m_ctx;
	int m_fd = -1;
	JSPromiseDesc m_read;

	/* factory JS methods */
	static JSValue onFDReadable(JSContext *ctx, JSValueConst this_val,
	    int argc, JSValueConst *argv);

	/* instance JS methods */
	JSValue ready(JSContext *ctx, JSValueConst this_val, int argc,
	    JSValueConst *argv);

	/* instance C++ methods */
	void app_cb(int fd);
	void clear();
	void finalizer();
	void gc_mark(JSRuntime *rt, JS_MarkFunc *mark_func);
};

/** Convert an array of JSValue to a vector of JSValue. */
std::vector<JSValue> argv_to_vec(JSContext *ctx, int argc, JSValueConst argv[]);

/** Unref and set to JS_UNDEFINED a JSValue. */
void JS_UnRef(JSContext *ctx, JSValue *val);

/**
 * Forward a call to an instance method of the 1st JS argument's associated
 * opaque value.
 */
template <typename T,
    JSValue (T::*fun)(JSContext *, JSValueConst, int, JSValueConst *)>
JSValue
first_arg_fun(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	T *obj = (T *)JS_GetOpaque2(ctx, argv[0], T::clsid);
	return (obj->*fun)(ctx, this_val, argc, argv);
}

template <typename T>
void
JSCXXClass<T>::finalizer_s(JSRuntime *rt, JSValue val)
{
	T *obj = (T *)JS_GetOpaque(val, T::clsid);
	if (obj)
		obj->finalizer();
}

template <typename T>
void
JSCXXClass<T>::gc_mark_s(JSRuntime *rt, JSValueConst val,
    JS_MarkFunc *mark_func)
{
	T *obj = (T *)JS_GetOpaque(val, T::clsid);
	if (obj)
		obj->gc_mark(rt, mark_func);
}

template <typename T>
void
JSCXXClass<T>::mod_init(JSContext *ctx, JSModuleDef *mod)
{
	JS_NewClassID(&T::clsid);
	JS_NewClass(JS_GetRuntime(ctx), T::clsid, &T::cls);
	JS_SetModuleExportList(ctx, mod, T::funcs, T::nfuncs);
}

template <typename T>
void
JSCXXClass<T>::mod_export(JSContext *ctx, JSModuleDef *mod)
{
	JS_AddModuleExportList(ctx, mod, T::funcs, T::nfuncs);
}

#endif /* JS2_H_ */

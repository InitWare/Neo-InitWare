#ifndef JS_RESTARTER_H_
#define JS_RESTARTER_H_

#include <memory>

#include "../restarters/restarter.h"
#include "js2.h"

class JSRestarter : public Restarter,
		    public JSCXXClass<JSRestarter>,
		    public std::enable_shared_from_this<JSRestarter> {
	friend class JSCXXClass<JSRestarter>;

    public:
	static JSClassID clsid;

    protected:
	static JSClassDef cls;
	static JSCFunctionListEntry funcs[];
	static size_t nfuncs;

	JSContext *m_ctx;
	JSValue m_obj;
	JSValue m_delegate; /**< the actual restarter object */

	/**
	 * JS factory methods
	 */
	static JSValue create(JSContext *ctx, JSValueConst this_val, int argc,
	    JSValueConst *argv);

	/**
	 * JS instance methods
	 */
	JSValue setForType(JSContext *ctx, JSValueConst this_val, int argc,
	    JSValueConst *argv);

	/**
	 * C++ instance methods
	 */
	JSRestarter(JSValue obj, Scheduler &sched)
	    : m_obj(obj)
	    , Restarter(sched) {};

	bool start(Transaction::Job::Id obj);
	bool stop(Transaction::Job::Id obj);

	void finalizer();
	void gc_mark(JSRuntime *rt, JS_MarkFunc *mark_func);
};

#endif /* JS_RESTARTER_H_ */

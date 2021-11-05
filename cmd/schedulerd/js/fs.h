#ifndef FS_HH_
#define FS_HH_

#include <memory>

#include "js2.h"

/**
 * Non-instantiable class implementing a Node.JS-like `fs' module.
 */
class JSFS : public JSCXXClass<JSFS> {
	friend class JSCXXClass<JSFS>;

    public:
	static JSClassID clsid;

    protected:
	static JSClassDef cls;
	static JSCFunctionListEntry funcs[];
	static size_t nfuncs;

	/**
	 * JS factory methods
	 */
	/** string -> string[] */
	static JSValue readdirSync(JSContext *ctx, JSValueConst this_val,
	    int argc, JSValueConst *argv);

	/** string -> string[] */
	static JSValue getLinkedNames(JSContext *ctx, JSValueConst this_val,
	    int argc, JSValueConst *argv);
};

#endif /* FS_HH_ */

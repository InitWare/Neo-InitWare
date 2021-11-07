#include <cassert>

#include "../app/app.h"
#include "cutils.h"
#include "js2.h"
#include "quickjs.h"

JSClassID JSFD::clsid;

void
JSFD::app_cb(int fd)
{
	JSValue j = JS_UNDEFINED;
	printf("Resolving read\n");
	m_read.resolve(m_ctx, 1, &j);
	finalizer();
}

void
JSFD::finalizer()
{
	if (m_fd != -1) {
		JS::from_context(m_ctx)->m_app->del_fd(m_fd);
		m_fd = -1;
	}
	delete (this);
}

void
JSFD::gc_mark(JSRuntime *rt, JS_MarkFunc *mark_func)
{
	m_read.gc_mark(rt, mark_func);
}

JSValue
JSFD::onFDReadable(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	JS *js = JS::from_context(ctx);
	JSValue obj, func;
	int64_t fd;
	JSFD *jsfd;
	int ret;

	if (JS_ToInt64(ctx, &fd, argv[0]))
		return JS_EXCEPTION;

	obj = JS_NewObjectClass(ctx, clsid);
	if (JS_IsException(obj))
		return obj;

	jsfd = new JSFD;
	if (!jsfd)
		return JS_ThrowOutOfMemory(ctx);

	ret = js->m_app->add_fd(fd, POLLIN,
	    std::bind(&JSFD::app_cb, jsfd, std::placeholders::_1));
	if (ret != 0) {
		delete jsfd;
		JS_FreeValue(ctx, obj);
		return JS_ThrowInternalError(ctx, "Failed to add error");
	}

	jsfd->m_ctx = ctx;
	jsfd->m_fd = fd;

	// JS_SetOpaque(obj, jsfd);

	return jsfd->m_read.init(ctx);
}

/*JSValue
JSFD::clearTimeout(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	clear();
	return JS_UNDEFINED;
}*/

JSClassDef JSFD::cls = {
	"FD",
	.finalizer = JSFD::finalizer_s,
	.gc_mark = JSFD::gc_mark_s,
};

JSCFunctionListEntry JSFD::funcs[] = {
	JS_CFUNC_DEF("onFDReadable", 1, JSFD::onFDReadable),
	/*JS_CFUNC_DEF("clearTimeout", 1,
	    (&first_arg_fun<JSFD, &JSFD::clearTimeout>)),
	JS_CFUNC_DEF("clearInterval", 1,
	    (&first_arg_fun<JSFD, &JSFD::clearTimeout>)),*/
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "FD", JS_PROP_CONFIGURABLE),
};

size_t JSFD::nfuncs = countof(JSFD::funcs);
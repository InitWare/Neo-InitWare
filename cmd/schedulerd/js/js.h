#ifndef JS_H_
#define JS_H_

#include <string>
#include <vector>

#include "quickjs.h"

class App;

struct JS {

    protected:
	static int mod_init(JSContext *ctx, JSModuleDef *mod);

    public:
	JSRuntime *m_rt;
	JSContext *m_ctx;
	App *m_app;

	static const char *tag_cstr(int64_t tag);

	static JS *from_context(JSContext *ctx);

	static void log_exception(JSContext *ctx);
	static void log_error(JSContext *ctx, JSValueConst exc);
	static void print_obj(JSContext *ctx, JSValueConst obj);

	static void eval(JSContext *ctx, std::string &filename,
	    std::string &txt, int eval_flags);

	/** Run any pending JavaScript jobs. Call after polling. */
	void run_pending_jobs();

	JS(App *app);
};

#endif /* JS_H_ */

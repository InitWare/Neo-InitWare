#ifndef JS_H_
#define JS_H_

#include <string>
#include <vector>

#include "quickjs.h"

class App;

namespace qjs {
class Runtime;
class Context;
};

struct JS {
    public:
	qjs::Runtime *rt;
	qjs::Context *ctx;
	App &m_app;

	static JS &from_ctx(JSContext *ctx);

	/** Run any pending JavaScript jobs. Call after polling. */
	void run_pending_jobs();
	/** Log the last exception for a context. */
	void log_exception(qjs::Context *ctx);

	JS(App &app);
};

#endif /* JS_H_ */

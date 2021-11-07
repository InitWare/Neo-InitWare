#include "../restarters/restarter.h"
#include "qjspp.h"

class JSRestarter : public Restarter {
	JSContext *ctx;

    public:
	JSRestarter(qjs::Value osched);
	bool start(Transaction::Job::Id obj);
	bool stop(Transaction::Job::Id obj);
};

JSRestarter::JSRestarter(qjs::Value osched)
    : Restarter(*osched.as<Scheduler *>())
    , ctx(osched.ctx)
{
}

bool
JSRestarter::start(Transaction::Job::Id obj)
{
	return qjs::Value(ctx, this)["startJob"]
	    .as<std::function<bool(int64_t)>>()(obj);
}

bool
JSRestarter::stop(Transaction::Job::Id obj)
{
	return qjs::Value(ctx, this)["stopJob"]
	    .as<std::function<bool(int64_t)>>()(obj);
}

void
setup_restarter(qjs::Context *ctx)
{
	qjs::Context::Module &mod = ctx->addModule("@iw/restarter");

	mod.class_<JSRestarter>("Restarter").constructor<qjs::Value>();
}
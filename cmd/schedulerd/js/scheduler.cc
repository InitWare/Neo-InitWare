#include "../app/app.h"
#include "../scheduler/scheduler.h"
#include "js.h"
#include "qjspp.h"

void
setup_sched(JS &js)
{
	qjs::Context::Module &mod = js.ctx->addModule("@iw/scheduler");
	auto scheduler = mod.class_<Scheduler>("Scheduler");
	auto edgeTypes = js.ctx->newObject();

	mod.add("edgeTypes", edgeTypes);
	mod.add("scheduler", &js.m_app.m_sched);

	scheduler.fun<&Scheduler::job_complete>("jobComplete");

#define EDGE(val) edgeTypes[#val] = (int64_t)Edge::Type::val
	EDGE(kAddStart);
	EDGE(kAddStartNonreq);
	EDGE(kAddVerify);
	EDGE(kAddStop);
	EDGE(kAddStopNonreq);
	EDGE(kPropagatesStopTo);
	EDGE(kPropagatesRestartTo);
	EDGE(kPropagatesReloadTo);
	EDGE(kStartOnStarted);
	EDGE(kTryStartOnStarted);
	EDGE(kStopOnStarted);
	EDGE(kStopOnStopped);
	EDGE(kOnSuccess);
	EDGE(kOnFailure);
	EDGE(kAfter);
	EDGE(kBefore);
}
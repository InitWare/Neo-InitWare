
#include <sys/event.h>

#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

#include "app/app.h"
#include "js/js.h"
#include "js/qjspp.h"

int
main(int argc, char *argv[])
{
	App app;

	try {
		app.m_js.ctx->evalFile(argv[1], JS_EVAL_TYPE_MODULE);
	} catch (qjs::exception exc) {
		app.m_js.log_exception(app.m_js.ctx);
	}

	//!! test code
	Schedulable::SPtr a;
	Schedulable::SPtr b;
	Schedulable::SPtr c;

	app.restarters["target"] = new TargetRestarter(app.m_sched);

	a = app.m_sched.add_object(std::make_shared<Schedulable>("a.target"));
	b = app.m_sched.add_object(std::make_shared<Schedulable>("b.target"));
	c = app.m_sched.add_object(std::make_shared<Schedulable>("c.target"));

	a->add_edge(Edge::Type(Edge::kAfter), c);
	b->add_edge(Edge::Type(Edge::kAfter | Edge::kAddStartNonreq), a);
	c->add_edge(Edge::Type(Edge::kAfter | Edge::kAddStart), b);

#if 0
	app.m_sched.to_graph(std::cout);

	app.m_sched.enqueue_tx(c, Transaction::JobType::kStart);
#endif

	//! test code ends

	return app.loop();
}


#include <sys/event.h>

#include <cassert>
#include <fstream>
#include <functional>
#include <sstream>

#include "app/app.h"
#include "js/js2.h"
#include "quickjs-libc.h"
#include "quickjs.h"

int
main(int argc, char *argv[])
{
	App app;
	std::ifstream t(argv[1]);
	std::stringstream buffer;
	std::string fname(argv[1]);
	std::string txt;

	buffer << t.rdbuf();
	txt = buffer.str();

	JS::eval(app.m_js.m_ctx, fname, txt, JS_EVAL_TYPE_MODULE);

	//!! test code
	Schedulable::SPtr a;
	Schedulable::SPtr b;
	Schedulable::SPtr c;

	a = app.m_sched.add_object(std::make_shared<Schedulable>("a"));
	b = app.m_sched.add_object(std::make_shared<Schedulable>("b"));
	c = app.m_sched.add_object(std::make_shared<Schedulable>("c"));

	/* b requires/after a */
	b->add_edge(Edge::kRequire, a);
	b->add_edge(Edge::kAfter, a);

	/* a requiredby b */
	a->add_edge(Edge::kPropagatesStopTo, b);

	/* c requires b */
	c->add_edge(Edge::kRequire, b);
	/* b requiredby c */
	b->add_edge(Edge::kPropagatesStopTo, c);

	//! test code ends

	return app.loop();
}
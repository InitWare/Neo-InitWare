
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

	return app.loop();
}
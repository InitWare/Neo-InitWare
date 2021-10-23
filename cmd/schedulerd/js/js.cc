
#include "js2.h"

std::vector<JSValue>
argv_to_vec(JSContext *ctx, int argc, JSValueConst argv[])
{
	std::vector<JSValue> vec;
	for (int i = 0; i < argc; i++)
		vec.push_back(JS_DupValue(ctx, argv[i]));
	return vec;
}

void
JS_UnRef(JSContext *ctx, JSValue *val)
{
	JS_FreeValue(ctx, *val);
	*val = JS_UNDEFINED;
}
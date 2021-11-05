#include <sys/param.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"
#include "quickjs.h"

#ifdef __NetBSD__
#define NOFOLLOW_SYMLINK_ERRNO EFTYPE
#else
#define NOFOLLOW_SYMLINK_ERRNO ELOOP
#endif

static int
get_symlinks(const char *path, std::list<std::string> &names)
{
	int depth = 0;

	for (;;) {
		char buf[MAXPATHLEN];
		int fd;
		int ret;

		if (depth++ >= 9)
			return -ELOOP;

		names.push_back(path);

		fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
		if (fd >= 0) {
			close(fd);
			return 0;
		}

		if (errno != NOFOLLOW_SYMLINK_ERRNO) {
			printf("open %s got got errno %s\n", path,
			    strerror(errno));
			return -errno;
		}

		// todo: legal to readlink into same buffer as source?
		ret = readlink(path, buf, MAXPATHLEN);
		if (ret < 0)
			return errno;
		else
			buf[ret] = '\0';

		path = buf;
	}
}

/**
 * JSFS proper
 */

JSValue
JSFS::readdirSync(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	JS *js = JS::from_context(ctx);
	JSValue arr = JS_NewArray(ctx);

	return arr;
}

JSValue
JSFS::getLinkedNames(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	JS *js = JS::from_context(ctx);
	JSValue arr = JS_NewArray(ctx);
	std::list<std::string> names;
	const char *path;
	int ret;
	int i = 0;

	path = JS_ToCString(ctx, argv[0]);
	if (!path)
		return JS_ThrowTypeError(ctx, "first arg must be string");

	ret = get_symlinks(path, names);
	if (ret < 0)
		return JS_ThrowInternalError(ctx, "errno %d", -ret);

	for (auto &name : names)
		JS_SetPropertyUint32(ctx, arr, i++,
		    JS_NewString(ctx, name.c_str()));

	return arr;
}

JSClassID JSFS::clsid;

JSClassDef JSFS::cls = {
	"FS",
};

JSCFunctionListEntry JSFS::funcs[] = {
	JS_CFUNC_DEF("readdirSync", 2, JSFS::readdirSync),
	JS_CFUNC_DEF("getLinkedNames", 1, JSFS::getLinkedNames),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "fs", JS_PROP_CONFIGURABLE),
};

size_t JSFS::nfuncs = countof(JSFS::funcs);
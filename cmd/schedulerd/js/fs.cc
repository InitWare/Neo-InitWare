#include <sys/param.h>

#include <cassert>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <libgen.h>
#include <unistd.h>

#include "fs.h"
#include "js2.h"
#include "quickjs.h"

#ifdef __NetBSD__
#define NOFOLLOW_SYMLINK_ERRNO EFTYPE
#else
#define NOFOLLOW_SYMLINK_ERRNO ELOOP
#endif

/** Like dirname(3) but doesn't modify source. Returns an malloc()'d string. */
char *
dirname_a(const char *path)
{
	char *path_copy, *dir, *dir_copy;

	if ((path_copy = strdup(path)) == NULL)
		return NULL;

	dir = dirname(path_copy);
	if (dir != path_copy) {
		dir_copy = strdup(dir);
		free(path_copy);
		return dir_copy;
	}

	return dir;
}

/** Is the path absolute? */
static bool
path_absolute(const char *path)
{
	return path[0] == '/';
}

/** Like readlink(3) but yields an absolute path. */
static ssize_t
readlink_absolute(const char *path, char *out)
{
	std::vector<char> buf(MAXPATHLEN);
	int ret;

	buf.reserve(MAXPATHLEN);

	ret = readlink(path, buf.data(), MAXPATHLEN);
	if (ret < 0)
		return -errno;
	else
		buf[ret] = '\0';

	if (!path_absolute(buf.data())) {
		char *dir = dirname_a(path);
		snprintf(out, MAXPATHLEN, "%s%s", dir, buf.data());
		free(dir);
	} else
		strcpy(out, buf.data());

	return 0;
}

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
			/* opened successfully with O_NOFOLLOW. */
			close(fd);
			return 0;
		} else if (errno != NOFOLLOW_SYMLINK_ERRNO) {
			printf("open %s got got errno %s\n", path,
			    strerror(errno));
			return -errno;
		}

		ret = readlink_absolute(path, buf);
		if (ret < 0)
			return errno;

		path = buf;
	}
}

struct IWJSCString {
	const char *str = nullptr;
	JSContext *ctx;

	IWJSCString(JSContext *ctx)
	    : ctx(ctx)
	{
	}

	IWJSCString &operator=(const char *txt)
	{
		assert(!str);
		str = txt;
		return *this;
	}

	~IWJSCString()
	{
		if (str)
			JS_FreeCString(ctx, str);
	}

	operator const char *() const { return str; }
};

struct IWJSValue {
	JSValue val = JS_UNDEFINED;
	JSContext *ctx;

	IWJSValue(JSContext *ctx)
	    : ctx(ctx)
	{
	}

	IWJSValue &operator=(JSValue newval)
	{
		if (!JS_IsUndefined(val))
			JS_FreeValue(ctx, val);
		val = newval;
		return *this;
	}

	~IWJSValue()
	{
		if (!JS_IsUndefined(val))
			JS_FreeValue(ctx, val);
	}

	operator JSValue() const { return val; }

	JSValue unmanage()
	{
		JSValue theval = val;
		val = JS_UNDEFINED;
		return theval;
	}
};

/**
 * JSFS proper
 */

JSValue
JSFS::readdirSync(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	JSValue arr = JS_NewArray(ctx);
	DIR *dir;
	struct dirent *dp;
	IWJSCString path(ctx);
	int i;

	path = JS_ToCString(ctx, argv[0]);
	if (path == NULL)
		JS_ThrowTypeError(ctx, "path must be string");

	dir = opendir(path);
	if (!dir)
		return JS_ThrowInternalError(ctx, "OpenDir: Errno %d", errno);

	while ((dp = readdir(dir)) != NULL) {
		JS_SetPropertyUint32(ctx, arr, i++,
		    JS_NewString(ctx, dp->d_name));
	}
	closedir(dir);

	return arr;
}

JSValue
JSFS::openSync(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	JS *js = JS::from_context(ctx);
	IWJSCString path(ctx);
	int64_t mode, flags;
	int fd;

	path = JS_ToCString(ctx, argv[0]);
	if (path == NULL)
		JS_ThrowTypeError(ctx, "path must be string");

	if (JS_ToInt64(ctx, &flags, argv[1]))
		JS_ThrowTypeError(ctx, "bad flags");
	else if (JS_ToInt64(ctx, &mode, argv[2]))
		JS_ThrowTypeError(ctx, "bad mode");

	if (mode & O_CREAT)
		fd = open(path, flags, mode);
	else
		fd = open(path, flags);

	if (fd < 0)
		return JS_ThrowInternalError(ctx, "Open: Errno %d", errno);
	else
		return JS_NewInt64(ctx, fd);
}

JSValue
JSFS::readSync(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
	JS *js = JS::from_context(ctx);
	int64_t fd, offset, length, position = -1;
	int nread;
	uint8_t *jsbuf;
	size_t lenJsbuf;

	jsbuf = JS_GetArrayBuffer(ctx, &lenJsbuf, argv[1]);
	if (jsbuf == NULL)
		return JS_ThrowTypeError(ctx, "bad buffer");
	else if (JS_ToInt64(ctx, &fd, argv[0]))
		return JS_ThrowTypeError(ctx, "bad fd");
	else if (JS_ToInt64(ctx, &offset, argv[2]))
		return JS_ThrowTypeError(ctx, "bad offset");
	else if (JS_ToInt64(ctx, &length, argv[3]))
		return JS_ThrowTypeError(ctx, "bad length");
	else if (!JS_IsUndefined(argv[4]) &&
	    JS_ToInt64(ctx, &position, argv[4]))
		return JS_ThrowTypeError(ctx, "bad position");

	if (lenJsbuf < (offset + length))
		return JS_ThrowRangeError(ctx, "read len > buf len");

	if (position == -1)
		nread = read(fd, jsbuf + offset, length);
	else
		nread = pread(fd, jsbuf + offset, length, position);

	if (nread == -1)
		return JS_ThrowInternalError(ctx, "Read: Errno %d", errno);
	else
		return JS_NewInt64(ctx, nread);
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

/* clang-format off */
JSCFunctionListEntry js_constants_funcs[] = {
	IWJS_CONST(O_RDONLY),
	IWJS_CONST(O_WRONLY),
	IWJS_CONST(O_RDWR),
	IWJS_CONST(O_ACCMODE),

	IWJS_CONST(O_NONBLOCK),
	IWJS_CONST(O_APPEND)
};
/* clang-format on */

JSCFunctionListEntry JSFS::funcs[] = {
	JS_OBJECT_DEF("constants", js_constants_funcs,
	    countof(js_constants_funcs),
	    JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE),
	JS_CFUNC_DEF("openSync", 2, JSFS::openSync),
	JS_CFUNC_DEF("readSync", 5, JSFS::readSync),
	JS_CFUNC_DEF("readdirSync", 2, JSFS::readdirSync),

	JS_CFUNC_DEF("getLinkedNames", 1, JSFS::getLinkedNames),
	JS_PROP_STRING_DEF("[Symbol.toStringTag]", "fs", JS_PROP_CONFIGURABLE),
};

size_t JSFS::nfuncs = countof(JSFS::funcs);
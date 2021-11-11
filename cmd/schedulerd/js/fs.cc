#include <sys/param.h>

#include <cassert>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <libgen.h>
#include <list>
#include <unistd.h>

#include "js.h"
#include "qjspp.h"
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
		snprintf(out, MAXPATHLEN, "%s/%s", dir, buf.data());
		free(dir);
	} else
		strcpy(out, buf.data());

	return 0;
}

static int
get_symlinks(const char *path, std::vector<std::string> &names)
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
		} else if (errno != NOFOLLOW_SYMLINK_ERRNO)
			return -errno;

		ret = readlink_absolute(path, buf);
		if (ret < 0)
			return -errno;

		path = buf;
	}
}

static qjs::Value
throwErrno(JSContext *ctx, int err)
{
	return qjs::Value(ctx,
	    JS_ThrowInternalError(ctx, "Errno %d: %s", err, strerror(err)));
}

/**
 * JSFS proper
 */

static qjs::Value
getLinkedNames(qjs::Value opath)
{
	JSContext *ctx = opath.ctx;
	std::string path = opath.as<std::string>();
	std::vector<std::string> names;
	int ret;
	int i = 0;

	ret = get_symlinks(path.c_str(), names);
	if (ret < 0)
		return throwErrno(ctx, -ret);

	return qjs::Value(ctx, names);
}

qjs::Value
openSync(qjs::Value opath, int64_t flags, int64_t mode)
{
	JSContext *ctx = opath.ctx;
	std::string path = opath.as<std::string>();
	int fd;

	if (mode & O_CREAT)
		fd = open(path.c_str(), flags, mode);
	else
		fd = open(path.c_str(), flags);

	if (fd < 0)
		return throwErrno(ctx, errno);
	else
		return qjs::Value(ctx, fd);
}

qjs::Value
readSync(int64_t fd, qjs::Value buffer, int64_t offset, int64_t length,
    int64_t position)
{
	JSContext *ctx = buffer.ctx;
	int nread;
	uint8_t *jsbuf;
	size_t lenJsbuf;

	jsbuf = JS_GetArrayBuffer(ctx, &lenJsbuf, buffer.v);
	if (jsbuf == NULL)
		return qjs::Value(ctx, JS_ThrowTypeError(ctx, "bad buffer"));

	if (lenJsbuf < (offset + length))
		return qjs::Value(ctx,
		    JS_ThrowRangeError(ctx, "read len > buf len"));

	if (position == -1)
		nread = read(fd, jsbuf + offset, length);
	else
		nread = pread(fd, jsbuf + offset, length, position);

	if (nread == -1)
		return throwErrno(ctx, errno);
	else
		return qjs::Value(ctx, nread);
}

static qjs::Value
readdirSync(qjs::Value opath)
{
	DIR *dir;
	struct dirent *dp;
	uint32_t i = 0;
	std::string path = opath.as<std::string>();
	auto jsarray = qjs::Value { opath.ctx, JS_NewArray(opath.ctx) };

	dir = opendir(path.c_str());
	if (!dir)
		return throwErrno(opath.ctx, errno);

	while ((dp = readdir(dir)) != NULL)
		jsarray[i++] = std::string(dp->d_name);
	closedir(dir);

	return jsarray;
}

void
setup_fs(qjs::Context *ctx)
{
	qjs::Context::Module &mod = ctx->addModule("@iw/fs");
	qjs::Value constants = ctx->newObject();

	mod.function<getLinkedNames>("getLinkedNames")
	    .function<openSync>("openSync")
	    .function<readSync>("readSync")
	    .function<readdirSync>("readdirSync")
	    .add("constants", constants);

#define CONST(val) constants[#val] = (int64_t)val;
	CONST(O_RDONLY);
	CONST(O_WRONLY);
	CONST(O_RDWR);
	CONST(O_ACCMODE);

	CONST(O_NONBLOCK);
	CONST(O_APPEND);
}
#include "addr2line.hh"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include "util.h"
}

#ifdef __APPLE__
static char *xstrndup(const char *str, size_t len)
{
	char *r;

	r = (char *) malloc(len + 1);
	if (r == NULL)
		return r;
	memcpy(r, str, len);
	r[len] = 0;
	return r;
}
#else
#define xstrndup strndup
#endif

Addr2line::Addr2line(const char *path)
{
	int out[2], in[2], check[2], child, r;

	if (pipe(out) < 0 || pipe(in) < 0 || pipe(check) < 0)
		edie("%s: pipe", __func__);
	if (fcntl(check[1], F_SETFD,
		  fcntl(check[1], F_GETFD, 0) | FD_CLOEXEC) < 0)
		edie("%s: fcntl", __func__);

	child = fork();
	if (child < 0) {
		edie("%s: fork", __func__);
	} else if (child == 0) {
		close(check[0]);
		dup2(out[0], 0);
		close(out[0]);
		close(out[1]);
		dup2(in[1], 1);
		close(in[0]);
		close(in[1]);

		r = execlp("addr2line", "addr2line", "-f", "-e", path, NULL);
		r = write(check[1], &r, sizeof(r));
		exit(0);
	}
	close(out[0]);
	close(in[1]);
	close(check[1]);

	if (read(check[0], &r, sizeof(r)) != 0) {
		errno = r;
		edie("%s: exec", __func__);
	}
	close(check[0]);

	_out = out[1];
	_in = in[0];
}

Addr2line::~Addr2line()
{
	close(_in);
	close(_out);
}

int
Addr2line::lookup(uint64_t pc, char **func, char **file, int *line)
{
	char buf[4096];
	int n = snprintf(buf, sizeof(buf), "%#"PRIx64"\n", pc);
	write(_out, buf, n);
	n = 0;
	while (1) {
		int r = read(_in, buf + n, sizeof(buf) - n - 1);
		if (r < 0)
			edie("%s: read", __func__);
		n += r;
		buf[n] = 0;

		int nls = 0;
		for (int i = 0; i < n; ++i)
			if (buf[i] == '\n')
				nls++;
		if (nls >= 2)
			break;
	}

	*func = *file = NULL;

	char *nl, *col, *end;
	nl = strchr(buf, '\n');
	*func = xstrndup(buf, nl - buf);
	col = strchr(nl, ':');
	if (!col)
		goto bad;
	*file = xstrndup(nl + 1, col - nl - 1);
	end = NULL;
	*line = strtol(col + 1, &end, 10);
	if (!end || *end != '\n')
		goto bad;

	return 0;

bad:
	free(*func);
	free(*file);
	*func = *file = NULL;
	*line = 0;
	return -1;
}

#define _GNU_SOURCE

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TAGSZ 512

enum {
	ENC_LATIN1,
	ENC_UTF16BOM,
	ENC_UTF16BE,
	ENC_UTF8
};

struct tag {
	char title[TAGSZ];
	char artist[TAGSZ];
};

static void
trim(char *s)
{
	char *p, *start;

	start = s;
	while (*start && (unsigned char)*start <= ' ')
		start++;
	p = start + strlen(start);
	while (p > start && (unsigned char)*(p - 1) <= ' ')
		p--;
	*p = '\0';
	if (start != s)
		memmove(s, start, (size_t)(p - start + 1));
}

static size_t
put_utf8(char *dst, size_t dlen, size_t di, unsigned int cp)
{
	if (cp < 0x20 || cp == 0x7F)
		cp = ' ';
	if (cp < 0x80) {
		if (di + 1 < dlen)
			dst[di++] = cp;
	} else if (cp < 0x800) {
		if (di + 2 < dlen) {
			dst[di++] = 0xC0 | (cp >> 6);
			dst[di++] = 0x80 | (cp & 0x3F);
		}
	} else if (cp < 0x10000) {
		if (di + 3 < dlen) {
			dst[di++] = 0xE0 | (cp >> 12);
			dst[di++] = 0x80 | ((cp >> 6) & 0x3F);
			dst[di++] = 0x80 | (cp & 0x3F);
		}
	} else if (cp <= 0x10FFFF) {
		if (di + 4 < dlen) {
			dst[di++] = 0xF0 | (cp >> 18);
			dst[di++] = 0x80 | ((cp >> 12) & 0x3F);
			dst[di++] = 0x80 | ((cp >> 6) & 0x3F);
			dst[di++] = 0x80 | (cp & 0x3F);
		}
	}
	return di;
}

static void
decode_latin1(char *dst, size_t dlen, const unsigned char *src, size_t slen)
{
	size_t di = 0, i;

	for (i = 0; i < slen && src[i]; i++)
		di = put_utf8(dst, dlen, di, src[i]);
	dst[di] = '\0';
}

static void
decode_utf8(char *dst, size_t dlen, const unsigned char *src, size_t slen)
{
	size_t di = 0, si = 0;
	unsigned int u;

	while (si < slen && src[si]) {
		if (src[si] < 0x80) {
			u = src[si++];
		} else if ((src[si] & 0xE0) == 0xC0 && si + 1 < slen) {
			u = ((src[si] & 0x1F) << 6) | (src[si + 1] & 0x3F);
			si += 2;
		} else if ((src[si] & 0xF0) == 0xE0 && si + 2 < slen) {
			u = ((src[si] & 0x0F) << 12) | ((src[si + 1] & 0x3F) << 6) | (src[si + 2] & 0x3F);
			si += 3;
		} else if ((src[si] & 0xF8) == 0xF0 && si + 3 < slen) {
			u = ((src[si] & 0x07) << 18) | ((src[si + 1] & 0x3F) << 12) | ((src[si + 2] & 0x3F) << 6) | (src[si + 3] & 0x3F);
			si += 4;
		} else {
			si++;
			continue;
		}
		di = put_utf8(dst, dlen, di, u);
	}
	dst[di] = '\0';
}

static void
decode_utf16(char *dst, size_t dlen, const unsigned char *src, size_t slen, int enc)
{
	size_t di = 0, si = 0;
	unsigned int u, u2;
	int be;

	be = (enc == ENC_UTF16BE);
	if (enc == ENC_UTF16BOM && slen >= 2) {
		if (src[0] == 0xFE && src[1] == 0xFF) {
			be = 1;
			si = 2;
		} else if (src[0] == 0xFF && src[1] == 0xFE) {
			be = 0;
			si = 2;
		}
	}
	for (; si + 1 < slen; si += 2) {
		u = be ? ((src[si] << 8) | src[si + 1]) : ((src[si + 1] << 8) | src[si]);
		if (!u)
			break;
		if (u >= 0xD800 && u <= 0xDBFF && si + 3 < slen) {
			u2 = be ? ((src[si + 2] << 8) | src[si + 3]) : ((src[si + 3] << 8) | src[si + 2]);
			if (u2 >= 0xDC00 && u2 <= 0xDFFF) {
				u = 0x10000 + (((u - 0xD800) << 10) | (u2 - 0xDC00));
				si += 2;
			}
		}
		if (u != 0xFEFF)
			di = put_utf8(dst, dlen, di, u);
	}
	dst[di] = '\0';
}

static void
decode_tag(char *dst, size_t dlen, const unsigned char *src, size_t slen, int enc)
{
	switch (enc) {
	case ENC_LATIN1:
		decode_latin1(dst, dlen, src, slen);
		break;
	case ENC_UTF16BOM:
	case ENC_UTF16BE:
		decode_utf16(dst, dlen, src, slen, enc);
		break;
	case ENC_UTF8:
		decode_utf8(dst, dlen, src, slen);
		break;
	default:
		dst[0] = '\0';
		break;
	}
	trim(dst);
}

static int
valid_id(const unsigned char *p)
{
	int i;

	for (i = 0; i < 4; i++) {
		if ((p[i] < 'A' || p[i] > 'Z') && (p[i] < '0' || p[i] > '9'))
			return 0;
	}
	return 1;
}

static size_t
be32(const unsigned char *p)
{
	return ((size_t)p[0] << 24) | ((size_t)p[1] << 16) | ((size_t)p[2] << 8) | p[3];
}

static size_t
sync_sz(const unsigned char *p)
{
	return ((size_t)(p[0] & 0x7F) << 21) | ((size_t)(p[1] & 0x7F) << 14) | ((size_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

static void
read_v2(FILE *f, struct tag *tp)
{
	char *dst;
	size_t pos, readsz, sz, tagsz;
	int ver;
	unsigned char buf[TAGSZ], h[10];

	if (fread(h, 1, sizeof(h), f) != sizeof(h) || memcmp(h, "ID3", 3))
		return;
	ver = h[3];
	if (ver != 3 && ver != 4)
		return;
	tagsz = sync_sz(h + 6);
	pos = 0;
	if ((h[5] & 0x40) && tagsz >= 4) {
		if (fread(buf, 1, 4, f) != 4)
			return;
		if (ver == 4) {
			sz = sync_sz(buf);
			if (sz < 4 || sz > tagsz || fseek(f, (long)(sz - 4), SEEK_CUR) < 0)
				return;
			pos = sz;
		} else {
			sz = be32(buf);
			if (sz > tagsz - 4 || fseek(f, (long)sz, SEEK_CUR) < 0)
				return;
			pos = sz + 4;
		}
	}
	while (pos + 10 <= tagsz && (!tp->title[0] || !tp->artist[0])) {
		if (fread(buf, 1, 10, f) != 10)
			break;
		if (!valid_id(buf))
			break;
		sz = (ver == 4) ? sync_sz(buf + 4) : be32(buf + 4);
		pos += 10;
		if (sz > tagsz - pos)
			break;
		dst = NULL;
		if (!tp->title[0] && !memcmp(buf, "TIT2", 4))
			dst = tp->title;
		else if (!tp->artist[0] && !memcmp(buf, "TPE1", 4))
			dst = tp->artist;
		readsz = 0;
		if (dst && sz > 1) {
			readsz = sz < sizeof(buf) ? sz : sizeof(buf);
			if (fread(buf, 1, readsz, f) == readsz)
				decode_tag(dst, sizeof(tp->title), buf + 1, readsz - 1, buf[0]);
		}
		if (sz > readsz && fseek(f, (long)(sz - readsz), SEEK_CUR) < 0)
			break;
		pos += sz;
	}
}

static void
read_v1(FILE *f, struct tag *tp)
{
	unsigned char v1[128];

	if (fseek(f, -128, SEEK_END) < 0)
		return;
	if (fread(v1, 1, sizeof(v1), f) != sizeof(v1) || memcmp(v1, "TAG", 3))
		return;
	if (!tp->title[0])
		decode_tag(tp->title, sizeof(tp->title), v1 + 3, 30, ENC_LATIN1);
	if (!tp->artist[0])
		decode_tag(tp->artist, sizeof(tp->artist), v1 + 33, 30, ENC_LATIN1);
}

static void
read_tags(FILE *f, struct tag *tp)
{
	tp->title[0] = '\0';
	tp->artist[0] = '\0';
	read_v2(f, tp);
	if (!tp->title[0] || !tp->artist[0])
		read_v1(f, tp);
}

static int
process_file(const char *path)
{
	FILE *f;
	struct tag tag;
	const char *base, *dot;
	size_t blen;

	if (!(f = fopen(path, "rbe"))) {
		warn("%s", path);
		return -1;
	}
	read_tags(f, &tag);
	fclose(f);
	if (!tag.title[0]) {
		base = strrchr(path, '/');
		base = base ? base + 1 : path;
		dot = strrchr(base, '.');
		if (dot && dot != base)
			blen = (size_t)(dot - base);
		else
			blen = strlen(base);
		decode_tag(tag.title, sizeof(tag.title), (const unsigned char *)base, blen, ENC_UTF8);
	}
	if (tag.artist[0])
		printf("#EXTINF:-1,%s - %s\n%s\n", tag.artist, tag.title, path);
	else
		printf("#EXTINF:-1,%s\n%s\n", tag.title, path);
	return 0;
}

static int
process_stream(FILE *fp)
{
	char *line = NULL;
	size_t sz = 0;
	ssize_t len;
	int ret = 0;

	while ((len = getline(&line, &sz, fp)) >= 0) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';
		if (len > 0 && process_file(line) < 0)
			ret = -1;
	}
	free(line);
	if (ferror(fp)) {
		warn("stdin");
		ret = -1;
	}
	return ret;
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [file ...]\n", program_invocation_short_name);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int ret = 0;

	opterr = 0;
	while (getopt(argc, argv, "") >= 0)
		usage();
	argc -= optind;
	argv += optind;
	puts("#EXTM3U");
	if (!argc && process_stream(stdin) < 0)
		ret = 1;
	for (; argc > 0; argv++, argc--) {
		if (!strcmp(*argv, "-")) {
			if (process_stream(stdin) < 0)
				ret = 1;
		} else if (process_file(*argv) < 0) {
			ret = 1;
		}
	}
	if (ferror(stdout))
		ret = 1;
	return ret;
}

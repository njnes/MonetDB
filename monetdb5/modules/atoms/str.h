/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2020 MonetDB B.V.
 */

#ifndef __string_H__
#define __string_H__
#include "gdk.h"
#include "mal.h"
#include "mal_exception.h"
#include <ctype.h>

/* The batstr module functions use a single buffer to avoid malloc/free overhead.
   Note the buffer should be always large enough to hold null strings, so less testing will be required */
#define INITIAL_STR_BUFFER_LENGTH  MAX(strlen(str_nil) + 1, 1024)
#define INITIAL_INT_BUFFER_LENGTH  1024 * sizeof(int)

/* The batstr module functions use a single buffer to avoid malloc/free overhead.
   Note the buffer should be always large enough to hold null strings, so less testing will be required */
#define CHECK_STR_BUFFER_LENGTH(BUFFER, BUFFER_LEN, NEXT_LEN, OP) \
	do {  \
		if (NEXT_LEN > *BUFFER_LEN) { \
			size_t newlen = NEXT_LEN + 1024; \
			str newbuf = GDKmalloc(newlen); \
			if (!newbuf) \
				throw(MAL, OP, SQLSTATE(HY013) MAL_MALLOC_FAIL); \
			GDKfree(*BUFFER); \
			*BUFFER = newbuf; \
			*BUFFER_LEN = newlen; \
		} \
	} while (0)

#define CHECK_INT_BUFFER_LENGTH(BUFFER, BUFFER_LEN, NEXT_LEN, OP) \
	do {  \
		if (NEXT_LEN > *BUFFER_LEN) { \
			size_t newlen = NEXT_LEN + (1024 * sizeof(int)); \
			int *newbuf = GDKmalloc(newlen); \
			if (!newbuf) \
				throw(MAL, OP, SQLSTATE(HY013) MAL_MALLOC_FAIL); \
			GDKfree(*BUFFER); \
			*BUFFER = newbuf; \
			*BUFFER_LEN = newlen; \
		} \
	} while (0)

extern int str_utf8_length(const char *s);
extern int str_nbytes(const char *s);

extern str str_from_wchr(str *buf, size_t *buflen, int c);
extern str str_wchr_at(int *res, const char *s, int at);

extern bit str_is_prefix(const char *s, const char *prefix);
extern bit str_is_suffix(const char *s, const char *suffix);

extern str str_tail(str *buf, size_t *buflen, const char *s, int off);
extern str str_Sub_String(str *buf, size_t *buflen, const char *s, int off, int l);
extern str str_substring_tail(str *buf, size_t *buflen, const char *s, int start);
extern str str_sub_string(str *buf, size_t *buflen, const char *s, int start, int l);
extern str str_suffix(str *buf, size_t *buflen, const char *s, int l);
extern str str_repeat(str *buf, size_t *buflen, const char *s, int c);

extern str str_lower(str *buf, size_t *buflen, const char *s);
extern str str_upper(str *buf, size_t *buflen, const char *s);

extern str str_strip(str *buf, size_t *buflen, const char *s);
extern str str_ltrim(str *buf, size_t *buflen, const char *s);
extern str str_rtrim(str *buf, size_t *buflen, const char *s);
extern str str_strip2(str *buf, size_t *buflen, int **chars, size_t *nchars, const char *s, const char *s2);
extern str str_ltrim2(str *buf, size_t *buflen, int **chars, size_t *nchars, const char *s, const char *s2);
extern str str_rtrim2(str *buf, size_t *buflen, int **chars, size_t *nchars, const char *s, const char *s2);
extern str str_lpad(str *buf, size_t *buflen, const char *s, int len);
extern str str_rpad(str *buf, size_t *buflen, const char *s, int len);
extern str str_lpad2(str *buf, size_t *buflen, const char *s, int len, const char *s2);
extern str str_rpad2(str *buf, size_t *buflen, const char *s, int len, const char *s2);

extern int str_search(const char *s, const char *s2);
extern int str_reverse_str_search(const char *s, const char *s2);
extern int str_locate2(const char *needle, const char *haystack, int start);

extern str str_splitpart(str *buf, size_t *buflen, const char *s, const char *s2, int f);
extern str str_insert(str *buf, size_t *buflen, const char *s, int strt, int l, const char *s2);
extern str str_substitute(str *buf, size_t *buflen, const char *s, const char *src, const char *dst, bit repeat);

#endif /* __string_H__ */

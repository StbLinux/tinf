/*
 * tinf unit test
 *
 * Copyright (c) 2014-2019 Joergen Ibsen
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must
 *      not claim that you wrote the original software. If you use this
 *      software in a product, an acknowledgment in the product
 *      documentation would be appreciated but is not required.
 *
 *   2. Altered source versions must be plainly marked as such, and must
 *      not be misrepresented as being the original software.
 *
 *   3. This notice may not be removed or altered from any source
 *      distribution.
 */

#include "tinf.h"

#include <stdlib.h>
#include <stdio.h>

#include "greatest.h"

#ifndef ARRAY_SIZE
#  define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

static const unsigned char robuffer[] = { 0 };

static unsigned char buffer[4096];

struct packed_data {
	unsigned int src_size;
	unsigned int depacked_size;
	const unsigned char data[32];
};

static const struct packed_data inflate_errors[] = {
	/* Unable to read first byte */
	{ 0, 1, { 0x42 } },
	/* No next block after non-final block */
	{ 5, 1, { 0x00, 0x00, 0x00, 0xFF, 0xFF } },
	/* Invalid block type 11 */
	{ 13, 1, { 0x07, 0xCA, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0xFF, 0x6B, 0x01, 0x00 } },

	/* Uncompressed block incomplete */
	{ 4, 1, { 0x01, 0x00, 0x00, 0xFF } },
	/* Uncompressed block inv length wrong */
	{ 5, 1, { 0x01, 0x00, 0x00, 0x00, 0x00 } },
	/* Uncompressed block missing data */
	{ 5, 1, { 0x01, 0x01, 0x00, 0xFE, 0xFF } },
	/* Uncompressed block writing one past end */
	{ 7, 1, { 0x01, 0x02, 0x00, 0xFD, 0xFF, 0x42, 0x42 } },

	/* Static incomplete */
	{ 2, 1, { 0x63, 0x00 } },
	/* Static reading one byte before start */
	{ 4, 4, { 0x63, 0x00, 0x42, 0x00 } },
	/* Static literal writing one byte past end */
	{ 4, 1, { 0x63, 0x60, 0x00, 0x00 } },
	/* Static match writing one byte past end */
	{ 4, 3, { 0x63, 0x00, 0x02, 0x00 } },
	/* Static len > 285 */
	{ 4, 1024, { 0x63, 0x18, 0x03, 0x00 } },
	/* Static dist > 29 */
	{ 4, 4, { 0x63, 0x00, 0x3E, 0x00 } },

	/* Dynamic incomplete no HDIST */
	{ 1, 1, { 0x05 } },
	/* Dynamic incomplete HCLEN */
	{ 2, 1, { 0x05, 0x00 } },
	/* Dynamic incomplete code length code lengths */
	{ 4, 1, { 0x05, 0x40, 0x00, 0x04 } },
	/* Dynamic code length code lengths all zero*/
	{ 6, 1, { 0x05, 0x0B, 0x00, 0x00, 0x00, 0x00 } },
	/* Dynamic incomplete literal code lengths */
	{ 4, 1, { 0x05, 0x20, 0x00, 0x04 } },
	/* Dynamic 256 has code length 0 */
	{ 13, 1, { 0x05, 0xCB, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFF, 0xD7, 0x02, 0x00 } },
	/* Dynamic only 256 available, but data contains 1 bit */
	{ 13, 1, { 0x05, 0xCA, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0x20, 0x7F, 0xEB, 0x00, 0x02 } },
	/* Dynamic only one distance code, but compressed data contains 1 bit */
	{ 13, 4, { 0x0D, 0xC0, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFC, 0xA9, 0x3F, 0x0F } },
	/* Dynamic all distance codes zero, but compressed data contains match */
	{ 14, 4, { 0x0D, 0xCA, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFC, 0xA9, 0x1F, 0xC0, 0x02 } },
	/* Dynamic only one code length code length, but compressed data contains 1 bit */
	{ 8, 4, { 0x05, 0x00, 0x80, 0xC0, 0xBF, 0x37, 0x00, 0x00 } },
	/* Dynamic first code length code is copy prev length  */
	{ 13, 1, { 0x05, 0xCA, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0xF1, 0x87, 0x0E, 0x00 } },
	/* Dynamic underfull code length in code length code (missing len 2 code) */
	{ 13, 1, { 0x05, 0xCA, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x7F, 0xEB, 0x00, 0x00 } },
	/* Dynamic overfull code length in code length code (extra len 2 code) */
	{ 13, 1, { 0x05, 0xCA, 0x81, 0x00, 0x00, 0x00, 0x00, 0x82, 0x20, 0x7F, 0xEB, 0x00, 0x00 } },
	/* Dynamic overfull code length in literal/length code (extra len 1 codes) */
	{ 15, 4, { 0x0D, 0xC3, 0x37, 0x01, 0x00, 0x00, 0x00, 0x80, 0x20, 0x46, 0xFF, 0xCE, 0xCA, 0x61, 0x01 } },
	/* Dynamic underfull code length in distance code (missing len 2 code) */
	{ 14, 4, { 0x0D, 0xCE, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFD, 0xA9, 0xBB, 0x09, 0x1A } },
	/* Dynamic overfull code length in distance code (extra len 2 code) */
	{ 15, 4, { 0x0D, 0xCE, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFD, 0xA9, 0xBB, 0x1F, 0xA0, 0x01 } },
	/* Dynamic HLIT too large (30 = 287) */
	{ 15, 4, { 0xF5, 0xCB, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFC, 0xA9, 0x9F, 0x24, 0x00, 0x01 } },
	/* Dynamic HDIST too large (30 = 31) */
	{ 15, 4, { 0xED, 0xDE, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFC, 0xA9, 0x5F, 0x24, 0x13, 0x01 } },
	/* Dynamic number of literal/length codes too large (last repeat exceeds limit) */
	{ 15, 4, { 0x0D, 0xCB, 0x37, 0x01, 0x00, 0x00, 0x00, 0x80, 0x20, 0xFA, 0xA7, 0x56, 0x08, 0x60, 0x01 } }
};

/* tinflate */

TEST inflate_empty_no_literals(void)
{
	/* Empty buffer, dynamic with 256 as only literal/length code
	 *
	 * You could argue that since the RFC only has an exception allowing
	 * one symbol for the distance tree, the literal/length tree should
	 * be complete. However gzip allows this.
	 *
	 * See also: https://github.com/madler/zlib/issues/75
	 */
	static const unsigned char data[] = {
		0x05, 0xCA, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0xFF,
		0x6B, 0x01, 0x00
	};
	unsigned int dlen = 0;
	int res;

	res = tinf_uncompress((void *) robuffer, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 0);

	PASS();
}

TEST inflate_huffman_only(void)
{
	/* 256 zero bytes compressed using Huffman only (no match or distance codes) */
	static const unsigned char data[] = {
		0x05, 0xCA, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFF,
		0xD5, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x02
	};
	unsigned char out[256];
	unsigned int dlen = ARRAY_SIZE(out);
	int res;
	int i;

	memset(out, 0xFF, ARRAY_SIZE(out));

	res = tinf_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == ARRAY_SIZE(out));

	for (i = 0; i < ARRAY_SIZE(out); ++i) {
		if (out[i]) {
			FAIL();
		}
	}

	PASS();
}

TEST inflate_rle(void)
{
	/* 256 zero bytes compressed using RLE (only one distance code) */
	static const unsigned char data[] = {
		0xE5, 0xC0, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFC,
		0xA9, 0x07, 0x39, 0x73, 0x01
	};
	unsigned char out[256];
	unsigned int dlen = ARRAY_SIZE(out);
	int res;
	int i;

	memset(out, 0xFF, ARRAY_SIZE(out));

	res = tinf_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == ARRAY_SIZE(out));

	for (i = 0; i < ARRAY_SIZE(out); ++i) {
		if (out[i]) {
			FAIL();
		}
	}

	PASS();
}

TEST inflate_max_matchlen(void)
{
	/* 259 zero bytes compressed using literal/length code 285 (len 258) */
	static const unsigned char data[] = {
		0xED, 0xCC, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFC,
		0xA9, 0x17, 0xB9, 0x00, 0x2C
	};
	unsigned char out[259];
	unsigned int dlen = ARRAY_SIZE(out);
	int res;
	int i;

	memset(out, 0xFF, ARRAY_SIZE(out));

	res = tinf_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == ARRAY_SIZE(out));

	for (i = 0; i < ARRAY_SIZE(out); ++i) {
		if (out[i]) {
			FAIL();
		}
	}

	PASS();
}

TEST inflate_max_matchlen_alt(void)
{
	/* 259 zero bytes compressed using literal/length code 284 + 31 (len 258)
	 *
	 * Technically, this is outside the range specified in the RFC, but
	 * gzip allows it.
	 *
	 * See also: https://github.com/madler/zlib/issues/75
	 */
	static const unsigned char data[] = {
		0xE5, 0xCC, 0x81, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA0, 0xFC,
		0xA9, 0x07, 0xB9, 0x00, 0xFC, 0x05
	};
	unsigned char out[259];
	unsigned int dlen = ARRAY_SIZE(out);
	int res;
	int i;

	memset(out, 0xFF, ARRAY_SIZE(out));

	res = tinf_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == ARRAY_SIZE(out));

	for (i = 0; i < ARRAY_SIZE(out); ++i) {
		if (out[i]) {
			FAIL();
		}
	}

	PASS();
}

TEST inflate_code_length_codes(void)
{
	/* 4 zero bytes compressed, code length codes include codes 16, 17, and 18 */
	static const unsigned char data[] = {
		0x0D, 0xC3, 0x37, 0x01, 0x00, 0x00, 0x00, 0x80, 0x20, 0xFA,
		0x77, 0x1E, 0xCA, 0x61, 0x01
	};
	unsigned char out[4];
	unsigned int dlen = ARRAY_SIZE(out);
	int res;
	int i;

	memset(out, 0xFF, ARRAY_SIZE(out));

	res = tinf_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == ARRAY_SIZE(out));

	for (i = 0; i < ARRAY_SIZE(out); ++i) {
		if (out[i]) {
			FAIL();
		}
	}

	PASS();
}

/* Test tinf_uncompress on compressed data with errors */
TEST inflate_error_case(const void *closure)
{
	const struct packed_data *pd = (const struct packed_data *) closure;
	int res;

	unsigned int size = pd->depacked_size;
	res = tinf_uncompress(buffer, &size, pd->data, pd->src_size);

	ASSERT(res != TINF_OK);

	PASS();
}

SUITE(tinflate)
{
	char suffix[32];
	int i;

	RUN_TEST(inflate_empty_no_literals);
	RUN_TEST(inflate_huffman_only);
	RUN_TEST(inflate_rle);
	RUN_TEST(inflate_max_matchlen);
	RUN_TEST(inflate_max_matchlen_alt);
	RUN_TEST(inflate_code_length_codes);

	for (i = 0; i < ARRAY_SIZE(inflate_errors); ++i) {
		sprintf(suffix, "%d", i);
		greatest_set_test_suffix(suffix);
		RUN_TEST1(inflate_error_case, &inflate_errors[i]);
	}
}

/* tinfzlib */

TEST zlib_empty_raw(void)
{
	/* Empty buffer, uncompressed */
	static const unsigned char data[] = {
		0x78, 0x9C, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
		0x01
	};
	unsigned int dlen = 0;
	int res;

	res = tinf_zlib_uncompress((void *) robuffer, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 0);

	PASS();
}

TEST zlib_empty_static(void)
{
	/* Empty buffer, static huffman */
	static const unsigned char data[] = {
		0x78, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01
	};
	unsigned int dlen = 0;
	int res;

	res = tinf_zlib_uncompress((void *) robuffer, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 0);

	PASS();
}

TEST zlib_empty_dynamic(void)
{
	/* Empty buffer, dynamic huffman */
	static const unsigned char data[] = {
		0x78, 0x9C, 0x05, 0xC1, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x10, 0xFF, 0xD5, 0x08, 0x00, 0x00, 0x00, 0x01
	};
	unsigned int dlen = 0;
	int res;

	res = tinf_zlib_uncompress((void *) robuffer, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 0);

	PASS();
}

TEST zlib_onebyte_raw(void)
{
	/* One byte 00, uncompressed */
	static const unsigned char data[] = {
		0x78, 0x9C, 0x01, 0x01, 0x00, 0xFE, 0xFF, 0x00, 0x00, 0x01,
		0x00, 0x01
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_zlib_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST zlib_onebyte_static(void)
{
	/* One byte 00, static huffman */
	static const unsigned char data[] = {
		0x78, 0x9C, 0x63, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_zlib_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST zlib_onebyte_dynamic(void)
{
	/* One byte 00, dynamic huffman */
	static const unsigned char data[] = {
		0x78, 0x9C, 0x05, 0xC1, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x10, 0xFF, 0xD5, 0x10, 0x00, 0x01, 0x00, 0x01
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_zlib_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST zlib_zeroes(void)
{
	/* 256 zero bytes, to test unrolling in Adler-32 */
	static const unsigned char data[] = {
		0x78, 0x9C, 0x63, 0x60, 0x18, 0xD9, 0x00, 0x00, 0x01, 0x00,
		0x00, 0x01
	};
	unsigned char out[256];
	unsigned int dlen = ARRAY_SIZE(out);
	int res;
	int i;

	memset(out, 0xFF, ARRAY_SIZE(out));

	res = tinf_zlib_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == ARRAY_SIZE(out));

	for (i = 0; i < ARRAY_SIZE(out); ++i) {
		if (out[i]) {
			FAIL();
		}
	}

	PASS();
}

SUITE(tinfzlib)
{
	RUN_TEST(zlib_empty_raw);
	RUN_TEST(zlib_empty_static);
	RUN_TEST(zlib_empty_dynamic);

	RUN_TEST(zlib_onebyte_raw);
	RUN_TEST(zlib_onebyte_static);
	RUN_TEST(zlib_onebyte_dynamic);
	RUN_TEST(zlib_zeroes);
}

/* tinfgzip */

TEST gzip_empty_raw(void)
{
	/* Empty buffer, uncompressed */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x01, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00
	};
	unsigned int dlen = 0;
	int res;

	res = tinf_gzip_uncompress((void *) robuffer, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 0);

	PASS();
}

TEST gzip_empty_static(void)
{
	/* Empty buffer, static huffman */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	unsigned int dlen = 0;
	int res;

	res = tinf_gzip_uncompress((void *) robuffer, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 0);

	PASS();
}

TEST gzip_empty_dynamic(void)
{
	/* Empty buffer, dynamic huffman */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x05, 0xC1, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFF,
		0xD5, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	unsigned int dlen = 0;
	int res;

	res = tinf_gzip_uncompress((void *) robuffer, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 0);

	PASS();
}

TEST gzip_onebyte_raw(void)
{
	/* One byte 00, uncompressed */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x01, 0x01, 0x00, 0xFE, 0xFF, 0x00, 0x8D, 0xEF, 0x02, 0xD2,
		0x01, 0x00, 0x00, 0x00
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_gzip_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST gzip_onebyte_static(void)
{
	/* One byte 00, static huffman */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x63, 0x00, 0x00, 0x8D, 0xEF, 0x02, 0xD2, 0x01, 0x00, 0x00,
		0x00
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_gzip_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST gzip_onebyte_dynamic(void)
{
	/* One byte 00, dynamic huffman */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x05, 0xC1, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xFF,
		0xD5, 0x10, 0x8D, 0xEF, 0x02, 0xD2, 0x01, 0x00, 0x00, 0x00
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_gzip_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST gzip_fhcrc(void)
{
	/* One byte 00, uncompressed, fhcrc */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x17, 0x9D, 0x01, 0x01, 0x00, 0xFE, 0xFF, 0x00, 0x8D, 0xEF,
		0x02, 0xD2, 0x01, 0x00, 0x00, 0x00
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_gzip_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST gzip_fextra(void)
{
	/* One byte 00, uncompressed, fextra */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x04, 0x00, 0x64, 0x61, 0x74, 0x61, 0x01, 0x01, 0x00, 0xFE,
		0xFF, 0x00, 0x8D, 0xEF, 0x02, 0xD2, 0x01, 0x00, 0x00, 0x00
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_gzip_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST gzip_fname(void)
{
	/* One byte 00, uncompressed, fname */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x66, 0x6F, 0x6F, 0x2E, 0x63, 0x00, 0x01, 0x01, 0x00, 0xFE,
		0xFF, 0x00, 0x8D, 0xEF, 0x02, 0xD2, 0x01, 0x00, 0x00, 0x00
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_gzip_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

TEST gzip_fcomment(void)
{
	/* One byte 00, uncompressed, fcomment */
	static const unsigned char data[] = {
		0x1F, 0x8B, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B,
		0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x00, 0x01, 0x01, 0x00, 0xFE,
		0xFF, 0x00, 0x8D, 0xEF, 0x02, 0xD2, 0x01, 0x00, 0x00, 0x00
	};
	unsigned char out[] = { 0xFF };
	unsigned int dlen = 1;
	int res;

	res = tinf_gzip_uncompress(out, &dlen, data, ARRAY_SIZE(data));

	ASSERT(res == TINF_OK && dlen == 1 && out[0] == 0);

	PASS();
}

SUITE(tinfgzip)
{
	RUN_TEST(gzip_empty_raw);
	RUN_TEST(gzip_empty_static);
	RUN_TEST(gzip_empty_dynamic);

	RUN_TEST(gzip_onebyte_raw);
	RUN_TEST(gzip_onebyte_static);
	RUN_TEST(gzip_onebyte_dynamic);

	RUN_TEST(gzip_fhcrc);
	RUN_TEST(gzip_fextra);
	RUN_TEST(gzip_fname);
	RUN_TEST(gzip_fcomment);
}

GREATEST_MAIN_DEFS();

int main(int argc, char *argv[])
{
	GREATEST_MAIN_BEGIN();

	tinf_init();

	RUN_SUITE(tinflate);
	RUN_SUITE(tinfzlib);
	RUN_SUITE(tinfgzip);

	GREATEST_MAIN_END();
}

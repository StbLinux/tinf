/*
 * mkzdata - generate deflate compressed data testcases
 *
 * Copyright (c) 2014-2019 Joergen Ibsen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tinf.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct lsb_bitwriter {
	unsigned char *next_out;
	uint32_t tag;
	int bitcount;
};

static void
lbw_init(struct lsb_bitwriter *lbw, unsigned char *dst)
{
	lbw->next_out = dst;
	lbw->tag = 0;
	lbw->bitcount = 0;
}

static unsigned char*
lbw_finalize(struct lsb_bitwriter *lbw)
{
	/* Write bytes until no bits left in tag */
	while (lbw->bitcount > 0) {
		*lbw->next_out++ = lbw->tag;
		lbw->tag >>= 8;
		lbw->bitcount -= 8;
	}

	return lbw->next_out;
}

static void
lbw_flush(struct lsb_bitwriter *lbw, int num) {
	assert(num >= 0 && num <= 32);

	/* Write bytes until at least num bits free */
	while (lbw->bitcount > 32 - num) {
		*lbw->next_out++ = lbw->tag;
		lbw->tag >>= 8;
		lbw->bitcount -= 8;
	}

	assert(lbw->bitcount >= 0);
}

static void
lbw_putbits_no_flush(struct lsb_bitwriter *lbw, uint32_t bits, int num) {
	assert(num >= 0 && num <= 32 - lbw->bitcount);
	assert((bits & (~0ULL << num)) == 0);

	/* Add bits to tag */
	lbw->tag |= bits << lbw->bitcount;
	lbw->bitcount += num;
}

static void
lbw_putbits(struct lsb_bitwriter *lbw, uint32_t bits, int num) {
	lbw_flush(lbw, num);
	lbw_putbits_no_flush(lbw, bits, num);
}

static void
lbw_putbits_rev(struct lsb_bitwriter *lbw, uint32_t bits, int num) {
	lbw_flush(lbw, num);
	while (num-- > 0) {
		lbw_putbits_no_flush(lbw, (bits >> num) & 1, 1);
	}
}

// 256 00 bytes compressed with Z_RLE (one distance code)
void
write_256_rle(struct lsb_bitwriter *lbw)
{
	// bfinal
	lbw_putbits(lbw, 1, 1);

	// btype
	lbw_putbits(lbw, 2, 2);

	// hlit
	lbw_putbits(lbw, 28, 5);

	// hdist
	lbw_putbits(lbw, 0, 5);

	// hclen
	lbw_putbits(lbw, 14, 4);

	lbw_putbits(lbw, 0, 3); // 16
	lbw_putbits(lbw, 0, 3); // 17
	lbw_putbits(lbw, 1, 3); // 18
	lbw_putbits(lbw, 0, 3); // 0
	lbw_putbits(lbw, 0, 3); // 8
	lbw_putbits(lbw, 0, 3); // 7
	lbw_putbits(lbw, 0, 3); // 9
	lbw_putbits(lbw, 0, 3); // 6
	lbw_putbits(lbw, 0, 3); // 10
	lbw_putbits(lbw, 0, 3); // 5
	lbw_putbits(lbw, 0, 3); // 11
	lbw_putbits(lbw, 0, 3); // 4
	lbw_putbits(lbw, 0, 3); // 12
	lbw_putbits(lbw, 0, 3); // 3
	lbw_putbits(lbw, 0, 3); // 13
	lbw_putbits(lbw, 2, 3); // 2
	lbw_putbits(lbw, 0, 3); // 14
	lbw_putbits(lbw, 2, 3); // 1

	// code lengths for literal/length
	lbw_putbits_rev(lbw, 2, 2); // 0 has len 1

	lbw_putbits_rev(lbw, 0, 1); // repeat len 0 for 138 times
	lbw_putbits(lbw, 127, 7);

	lbw_putbits_rev(lbw, 0, 1); // repeat len 0 for 117 times
	lbw_putbits(lbw, 106, 7);

	lbw_putbits_rev(lbw, 3, 2); // 256 has len 2

	lbw_putbits_rev(lbw, 0, 1); // repeat len 0 for 27 times
	lbw_putbits(lbw, 16, 7);

	lbw_putbits_rev(lbw, 3, 2); // 284 has len 2

	// code lengths for distance
	lbw_putbits_rev(lbw, 2, 2); // 1 has len 1

	// compressed data
	lbw_putbits_rev(lbw, 0, 1); // 00 byte

	lbw_putbits_rev(lbw, 3, 2); // match len 255
	lbw_putbits(lbw, 28, 5);

	lbw_putbits_rev(lbw, 0, 1); // distance 1

	// end of block
	lbw_putbits_rev(lbw, 2, 2); // 256 = EOB
}

// 256 00 bytes compressed with Z_HUFFMAN_ONLY (no distance codes)
void
write_256_huffman(struct lsb_bitwriter *lbw)
{
	// bfinal
	lbw_putbits(lbw, 1, 1);

	// btype
	lbw_putbits(lbw, 2, 2);

	// hlit
	lbw_putbits(lbw, 0, 5);

	// hdist
	lbw_putbits(lbw, 10, 5);

	// hclen
	lbw_putbits(lbw, 14, 4);

	lbw_putbits(lbw, 0, 3); // 16
	lbw_putbits(lbw, 0, 3); // 17
	lbw_putbits(lbw, 1, 3); // 18
	lbw_putbits(lbw, 0, 3); // 0
	lbw_putbits(lbw, 0, 3); // 8
	lbw_putbits(lbw, 0, 3); // 7
	lbw_putbits(lbw, 0, 3); // 9
	lbw_putbits(lbw, 0, 3); // 6
	lbw_putbits(lbw, 0, 3); // 10
	lbw_putbits(lbw, 0, 3); // 5
	lbw_putbits(lbw, 0, 3); // 11
	lbw_putbits(lbw, 0, 3); // 4
	lbw_putbits(lbw, 0, 3); // 12
	lbw_putbits(lbw, 0, 3); // 3
	lbw_putbits(lbw, 0, 3); // 13
	lbw_putbits(lbw, 0, 3); // 2
	lbw_putbits(lbw, 0, 3); // 14
	lbw_putbits(lbw, 1, 3); // 1

	// code lengths for literal/length
	lbw_putbits_rev(lbw, 0, 1); // 0 has len 1

	lbw_putbits_rev(lbw, 1, 1); // repeat len 0 for 138 times
	lbw_putbits(lbw, 127, 7);

	lbw_putbits_rev(lbw, 1, 1); // repeat len 0 for 117 times
	lbw_putbits(lbw, 106, 7);

	lbw_putbits_rev(lbw, 0, 1); // 256 has len 1

	// code lengths for distance
	lbw_putbits_rev(lbw, 1, 1); // repeat len 0 for 11 times
	lbw_putbits(lbw, 0, 7);

	// compressed data
	for (int i = 0; i < 256; ++i) {
		lbw_putbits_rev(lbw, 0, 1); // 00 byte
	}

	// end of block
	lbw_putbits_rev(lbw, 1, 1); // 256 = EOB
}

// empty with no literal symbols and no distance codes (only 256 has len 1)
void
write_no_lit(struct lsb_bitwriter *lbw)
{
	// bfinal
	lbw_putbits(lbw, 1, 1);

	// btype
	lbw_putbits(lbw, 2, 2);

	// hlit
	lbw_putbits(lbw, 0, 5);

	// hdist
	lbw_putbits(lbw, 10, 5);

	// hclen
	lbw_putbits(lbw, 14, 4);

	lbw_putbits(lbw, 0, 3); // 16
	lbw_putbits(lbw, 0, 3); // 17
	lbw_putbits(lbw, 1, 3); // 18
	lbw_putbits(lbw, 0, 3); // 0
	lbw_putbits(lbw, 0, 3); // 8
	lbw_putbits(lbw, 0, 3); // 7
	lbw_putbits(lbw, 0, 3); // 9
	lbw_putbits(lbw, 0, 3); // 6
	lbw_putbits(lbw, 0, 3); // 10
	lbw_putbits(lbw, 0, 3); // 5
	lbw_putbits(lbw, 0, 3); // 11
	lbw_putbits(lbw, 0, 3); // 4
	lbw_putbits(lbw, 0, 3); // 12
	lbw_putbits(lbw, 0, 3); // 3
	lbw_putbits(lbw, 0, 3); // 13
	lbw_putbits(lbw, 0, 3); // 2
	lbw_putbits(lbw, 0, 3); // 14
	lbw_putbits(lbw, 1, 3); // 1

	// code lengths for literal/length
	lbw_putbits_rev(lbw, 1, 1); // repeat len 0 for 138 times
	lbw_putbits(lbw, 127, 7);

	lbw_putbits_rev(lbw, 1, 1); // repeat len 0 for 118 times
	lbw_putbits(lbw, 107, 7);

	lbw_putbits_rev(lbw, 0, 1); // 256 has len 1

	// code lengths for distance
	lbw_putbits_rev(lbw, 1, 1); // repeat len 0 for 11 times
	lbw_putbits(lbw, 0, 7);

	// no compressed data

	// end of block
	lbw_putbits_rev(lbw, 0, 1); // 256 = EOB
}

int
main(int argc, char *argv[])
{
	if (argc > 2) {
		fputs("syntax: mkzdata FILE\n", stderr);
		return EXIT_FAILURE;
	}

	const unsigned char org_data[1024] = {0};

	unsigned char data[4096];

	struct lsb_bitwriter lbw;

	lbw_init(&lbw, &data[0]);

	write_256_rle(&lbw);

	uint32_t size = lbw_finalize(&lbw) - &data[0];

	for (int i = 0; i < size; ++i) {
		if (i > 0) {
			fputs(", ", stdout);
		}
		printf("0x%02X", data[i]);
	}
	printf("\n");

	if (argc > 1) {
		FILE *fout = fopen(argv[1], "wb");

		const unsigned char gzip_header[10] = {
			0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0B
		};

		fwrite(gzip_header, 1, sizeof(gzip_header), fout);

		fwrite(data, 1, size, fout);

		// Note: only works on little-endian
		uint32_t crc = tinf_crc32(&org_data[0], 256);
		fwrite(&crc, sizeof(crc), 1, fout);

		uint32_t org_size = 256;
		fwrite(&org_size, sizeof(org_size), 1, fout);

		fclose(fout);
	}

	return 0;
}
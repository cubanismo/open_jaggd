/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Author: James Jones
 */

#ifndef FILEIO_H_
#define FILEIO_H_

#include <sys/types.h> /* off_t */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
	/* Local data */
	uint8_t *buf;
	size_t length;
	off_t offset;
	size_t dataSize;

	/* Jaguar-side data */
	uint32_t baseAddr;
	uint32_t execAddr;
} JagFile;

extern JagFile *LoadFile(const char *fileName);
extern void FreeFile(JagFile *jf);
extern FILE *PrepFile(const char *filePath, const char **dstFileName, uint32_t *size);

#endif /* FILEIO_H_ */

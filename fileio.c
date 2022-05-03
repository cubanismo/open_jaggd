/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Author: James Jones
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "fileio.h"

static inline uint32_t read32BE(const void *ptr)
{
	const uint8_t *data = ptr;

	return (uint32_t)data[0] << 24 |
		(uint32_t)data[1] << 16 |
		(uint32_t)data[2] << 8 |
		(uint32_t)data[3];
}

static bool IsRomHeader(const JagFile *jf, off_t offset, uint32_t *romExecAddr)
{
	uint8_t *ptr;
	uint32_t start;

	// Verify the file is big enough to contain a ROM header:
	if (jf->length <= (0x2000 + offset)) {
		return false;
	}

	// Jump to the MEMCON1 ROMWIDTH and ROMSPEED bytes:
	ptr = jf->buf + 0x400 + offset;

	// Verify the ROMWIDTH and ROMSPEED bytes are all the same:
	if ((ptr[0] != ptr[1]) ||
	    (ptr[0] != ptr[2]) ||
	    (ptr[0] != ptr[3])) {
		return false;
	}

	// Verify the ROMWIDTH and ROMSPEED values are sane:
	if (ptr[0] & ~(0x1e)) {
		return false;
	}

	// It looks like a ROM header. Extract the start address:
	start = read32BE(ptr+0x4);

	// Verify the start address is within the ROM1 memory region:
	if ((start < 0x800000) || (start >= 0xe00000)) {
		return false;
	}

	// Looks good. Save start address if necessary:
	if (romExecAddr) *romExecAddr = start;

	return true;
}

static bool InferFileInfo(JagFile *jf, const char *fileName)
{
	size_t fileNameLen = strlen(fileName);

	if (IsRomHeader(jf, 0x0, &jf->execAddr)) {
		jf->baseAddr = 0x800000;
		jf->offset = 0x0;
		jf->dataSize = jf->length - jf->offset;
		return true;
	}
	
	if (IsRomHeader(jf, 0x200, &jf->execAddr)) {
		jf->baseAddr = 0x800000;
		jf->offset = 0x200;
		jf->dataSize = jf->length - jf->offset;
		return true;
	}
	
	if ((jf->length > 0x48) &&
	    (jf->buf[0] == 0x01) && (jf->buf[1] == 0x50)) {
		/* COFF File */

		/* run header exec value */
		jf->execAddr = read32BE(jf->buf+0x24);

		/*
		 * run header text base address.
		 *
		 * NOTE: Don't use the text section header start address.
		 * JiFFI appears to hard-code that one to 0x4000.
		 */
		jf->baseAddr = read32BE(jf->buf+0x28);

		/* text section header offset */
		jf->offset = read32BE(jf->buf+0x44);

		/* XXX Assumes data section is contiguous with text on Jaguar */

		/* XXX Will read & transfer symbol sections too */
		jf->dataSize = jf->length - jf->offset;
		return true;
	}
	
	if ((jf->length > 0x30) && (jf->buf[0] == 0x7f) &&
	    (jf->buf[1] == 'E') && (jf->buf[2] == 'L') &&
	    (jf->buf[3] == 'F')) {
		/* XXX ELF File */
		return false;
	}
	
	if ((jf->length > 0x2e) && (jf->buf[0x1c] == 'J') &&
	    (jf->buf[0x1d] == 'A') && (jf->buf[0x1e] == 'G') &&
	    (jf->buf[0x1f] == 'R')) {
		/* Jag Server Executable */
		jf->baseAddr = read32BE(jf->buf+0x22);
		if (jf->buf[0x21] >= 0x03) {
			/* Version 3 has a separate start address in header */
			jf->execAddr = read32BE(jf->buf+0x2a);
			jf->offset = 0x2e;
		} else {
			/* In version 2 start address == base address */
			jf->execAddr = jf->baseAddr;
			jf->offset = 0x2a;
		}
		/* Size is also stored at jf->buf+0x26 */
		jf->dataSize = jf->length - jf->offset;
		return true;
	}
	
	if ((jf->length > 0x24) &&
	    (jf->buf[0] == 0x60) && (jf->buf[1] == 0x1b)) {
		/* DRI ABS File */
		jf->offset = 0x24;
		jf->baseAddr = read32BE(jf->buf+0x16);
		jf->execAddr = jf->baseAddr;
		jf->dataSize = read32BE(jf->buf+0x2) + read32BE(jf->buf+0x6);
		return true;
	}

	if (jf->length > 0x2000) {
		off_t i;

		/*
		 * If the first 8192 bytes (after the first 8) are all the same
		 * value, assume this is a padded headerless ROM file.
		 */
		for (i = 9; i < 0x2000; i++) {
			if (jf->buf[i] != jf->buf[8]) break;
		}

		/*
		 * Could generate false negative if weird padding bytes
		 * that match a 68k nop instruction are used. Unlikely.
		 */
		if ((i >= 0x2000) && (jf->buf[0x2000] != jf->buf[8])) {
			jf->baseAddr = 0x802000;
			jf->execAddr = jf->baseAddr;
			jf->offset = 0x2000;
			jf->dataSize = jf->length - jf->offset;
			return true;
		}
	}

	if (fileNameLen >= 4) {
		/* Assume *.rom files are 0x802000 start addr headerless ROMs */
		const char *fExt = &fileName[fileNameLen-4];

		if ((fExt[0] != '.') ||
				((fExt[1] != 'r') && (fExt[1] != 'R')) ||
				((fExt[2] != 'o') && (fExt[2] != 'O')) ||
				((fExt[3] != 'm') && (fExt[3] != 'M'))) {
			return false;
		}

		jf->baseAddr = 0x802000;
		jf->execAddr = jf->baseAddr;
		jf->offset = 0;
		jf->dataSize = jf->length;
		return true;
	}

	return false;
}

JagFile *LoadFile(const char *fileName)
{
	/* Refuse to load files > 17MB in size */
	static const size_t MAX_SIZE = 17 * 1024 * 1024;

	JagFile *jf = NULL;
	FILE *hFile = NULL;
	size_t fileSize = 0;
	bool done = false;

	hFile = fopen(fileName, "rb");

	if (!hFile) {
		fprintf(stderr, "Failed to open '%s':\n  %s\n",
			fileName, strerror(errno));
		goto cleanup;
	}

	if (fseek(hFile, 0, SEEK_END)) {
		fprintf(stderr, "Failed to find end of '%s':\n  %s\n",
			fileName, strerror(errno));
		goto cleanup;
	}

	fileSize = ftell(hFile);

	if (fileSize < 0) {
		fprintf(stderr, "Failed to query size of '%s':\n  %s\n",
			fileName, strerror(errno));
		goto cleanup;
	}

	if (fileSize > MAX_SIZE) {
		fprintf(stderr, "Refusing to load file of size %zd\n",
			fileSize);
		goto cleanup;
	}

	if (fseek(hFile, 0, SEEK_SET)) {
		fprintf(stderr, "Failed to seek to start of '%s':\n  %s\n",
			fileName, strerror(errno));
		goto cleanup;
	}

	jf = calloc(1, sizeof(*jf));

	if (!jf) {
		fprintf(stderr, "Failed to alloc file structure\n");
		goto cleanup;
	}

	jf->buf = malloc(fileSize);
	jf->length = fileSize;

	if (!jf->buf) {
		fprintf(stderr, "Failed to alloc %zd bytes for file buffer\n",
			fileSize);
		goto cleanup;
	}

	if (fread(jf->buf, 1, fileSize, hFile) != fileSize) {
		fprintf(stderr, "Failed to read %zd bytes from %s:\n  %s\n",
			fileSize, fileName, strerror(errno));
		goto cleanup;
	}

	if (!InferFileInfo(jf, fileName)) {
		jf->baseAddr = 0x4000;
		jf->execAddr = jf->baseAddr;
		jf->offset = 0;
		jf->dataSize = jf->length;
	}

	done = true;

cleanup:
	if (hFile) {
		fclose(hFile);
	}

	if (!done) {
		FreeFile(jf); jf = NULL;
	}

	return jf;
}

void FreeFile(JagFile *jf)
{
	if (jf) {
		free(jf->buf); jf->buf = NULL;
		free(jf);
	}
}

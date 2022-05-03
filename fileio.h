#ifndef FILEIO_H_
#define FILEIO_H_

#include <stdint.h>
#include <stdbool.h>

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

#endif /* FILEIO_H_ */

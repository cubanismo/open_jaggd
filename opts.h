/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Author: James Jones
 */

#ifndef OPTS_H_
#define OPTS_H_

#include <stdbool.h>
#include <stdint.h>

extern bool ParseOptions(int argc, char *argv[],
			 bool *oReset,
			 bool *oDebug,
			 bool *oBoot,
			 bool *oBootRom,
			 char **oFileName,
			 uint32_t *oBase,
			 uint32_t *oSize,
			 uint32_t *oOffset,
			 uint32_t *oExec);
#endif /* OPTS_H_ */

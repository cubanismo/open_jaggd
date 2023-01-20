/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Author: James Jones
 */

#ifndef OPTS_H_
#define OPTS_H_

#include <stdbool.h>
#include <stdint.h>

enum EJagGDCommand
{
  ECmd_ResetNone = -1,
  ECmd_Reset = 0,
  ECmd_ResetDebug,
  ECmd_Server,
  ECmd_WriteFile = 5,
  ECmd_ResetRom
};

enum EServerCommand
{
  ECmd_Upload = 4,
  ECmd_Execute,
  ECmd_EnableEEPROM
};

extern bool ParseOptions(int argc, char *argv[],
			 int *oReset,
			 bool *oDebug,
			 int *oBoot,
			 char **oFileName,
			 uint32_t *oBase,
			 uint32_t *oSize,
			 uint32_t *oOffset,
			 uint32_t *oExec);
#endif /* OPTS_H_ */

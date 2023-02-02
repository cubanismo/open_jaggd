/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Author: James Jones
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "opts.h"

static void usage(void)
{
	printf("Reverse-Engineered Jaguar GameDrive Utility.\n\n");

	printf("Usage: jaggd [commands]\n\n");

	printf("-r         Reboot\n");
	printf("-rd        Reboot to debug stub\n");
	printf("-rr        Reboot and keep current ROM\n\n");

	printf("From stub mode (all ROM, RAM > $2000) --\n");
	printf("-u[x[r]] file[,a:addr,s:size,o:offset,x:entry]\n");
	printf("           Upload to address with size and file offset and "
	       "optionally execute\n");
	printf("           directly or via reboot\n");
	printf("-e file[,size]\n");
	printf("           Enable EEPROM file on memory card with given size "
	       "in bytes (default 128)\n");
	printf("-x addr    Execute from address\n");
	printf("-xr        Execute via reboot\n\n");

	printf("Prefix numbers with '$' or '0x' for hex, otherwise decimal is "
	       "assumed.\n");
}

static bool ParseNumber(const char *str, uint32_t *num)
{
	int base = 10;
	char *end;
	uint32_t res;

	if (str[0] != '\0') {
		if (str[0] == '$') {
			base = 16;
			str++;
		} else if ((str[0] == '0') &&
			   ((str[1] == 'x') || (str[1] == 'X'))) {
			base = 16;
		}
	}

	res = strtol(str, &end, base);

	if ((str[0] != '\0') && (end[0] == '\0')) {
		*num = res;
		return true;
	} else {
		return false;
	}
}

static bool ParseFile(char *opt,
		      char **oFileName,
		      uint32_t *oBase,
		      uint32_t *oSize,
		      uint32_t *oOffset,
		      uint32_t *oExec)
{
	char *tok = strtok(opt, ",");
	size_t nameLen;

	if (!tok) {
		usage();
		return false;
	}

	nameLen = strlen(tok) + 1;

	*oFileName = malloc(nameLen);

	if (!*oFileName) {
		fprintf(stderr, "Failed to allocate %zd bytes for file name\n",
			nameLen);
		return false;
	}

	strcpy(*oFileName, tok);

	while ((tok = strtok(NULL, ","))) {
		bool subOptGood = false;

		if (!strncmp(tok, "a:", 2)) {
			subOptGood = ParseNumber(&tok[2], oBase);
		} else if (!strncmp(tok, "s:", 2)) {
			subOptGood = ParseNumber(&tok[2], oSize);
		} else if (!strncmp(tok, "o:", 2)) {
			subOptGood = ParseNumber(&tok[2], oOffset);
		} else if (!strncmp(tok, "x:", 2)) {
			subOptGood = ParseNumber(&tok[2], oExec);
		}

		if (!subOptGood) {
			free(*oFileName); *oFileName = NULL;
			return false;
		}
	}

	return true;
}

bool ParseOptions(int argc, char *argv[],
		  bool *oReset,
		  bool *oDebug,
		  bool *oBoot,
		  bool *oBootRom,
		  char **oFileName,
		  uint32_t *oBase,
		  uint32_t *oSize,
		  uint32_t *oOffset,
		  uint32_t *oExec,
		  char **oEepromName,
		  uint8_t *oEepromType)
{
	char *outName = NULL;
	char *outEeprom = NULL;
	int i;
	bool success = true;

	for (i = 1; i < argc; i++) {
		size_t optLen = strlen(argv[i]);

		if (!strncmp(argv[i], "-r", 2)) {
			if (optLen > 2) {
				if (argv[i][3] != '\0') {
					usage();
					success = false;
					break;
				}

				if (argv[i][2] == 'd') {
					*oDebug = true;
				} else if (argv[i][2] == 'r') {
					*oBootRom = true;
				} else {
					usage();
					success = false;
					break;
				}
			}

			*oReset = true;
		} else if (!strncmp(argv[i], "-u", 2)) {
			switch (optLen) {
			case 4:
				if (argv[i][3] == 'r') {
					*oBootRom = true;
				} else {
					success = false;
					break;
				}
				// Fall through
			case 3:
				if (argv[i][2] == 'x') {
					*oBoot = true;
				} else {
					success = false;
					break;
				}
				// Fall through
			case 2:
				break;

			default:
				success = false;
				break;
			}

			if (!success || (++i >= argc)) {
				usage();
				success = false;
				break;
			}

			if (!ParseFile(argv[i], &outName,
				       oBase, oSize, oOffset, oExec)) {
				usage();
				success = false;
				break;
			}
		} else if (!strcmp(argv[i], "-x")) {
			if (++i >= argc) {
				usage();
				success = false;
				break;
			}

			if (!ParseNumber(argv[i], oExec)) {
				usage();
				success = false;
				break;
			}

			*oBoot = true;
		} else if (!strcmp(argv[i], "-xr")) {
			*oBoot = true;
			*oBootRom = true;
		} else if (!strcmp(argv[i], "-e")) {
			char *tok;
			size_t nameLen;

			if (++i >= argc) {
				usage();
				success = false;
				break;
			}

			tok = strtok(argv[i], ",");

			if (!tok) {
				usage();
				success = false;
				break;
			}

			nameLen = strlen(tok) + 1;

			if (nameLen == 0) {
				usage();
				success = false;
				break;
			}

			outEeprom = malloc(nameLen);

			if (!outEeprom) {
				fprintf(stderr, "Failed to allocate %zu bytes for EEPROM name\n",
					nameLen);
				success = false;
				break;
			}

			strcpy(outEeprom, tok);

			tok = strtok(NULL, ",");

			if (tok) {
				uint32_t eepromSize;
				if (!ParseNumber(tok, &eepromSize)) {
					usage();
					success = false;
					break;
				}
				switch (eepromSize) {
				case 128:
					*oEepromType = 0;
					break;
				case 256:
				case 512:
					*oEepromType = 1;
					break;
				case 1024:
				case 2048:
					*oEepromType = 2;
					break;
				default:
					success = false;
					break;

				}

				if (!success) {
					usage();
					break;
				}
			}
		} else {
			usage();
			success = false;
			break;
		}
	}

	/* The user didn't ask us to do anything. Complain. */
	if (!*oReset && !outName && !*oBoot && !outEeprom) {
		usage();
		success = false;
	}

	if (!success) {
		free(outName); outName = NULL;
		free(outEeprom); outEeprom = NULL;
		return false;
	}

	*oFileName = outName;
	*oEepromName = outEeprom;
	return true;
}

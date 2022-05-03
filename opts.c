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
	printf("-rd        Reboot to debug stub\n\n");

	printf("From stub mode (all ROM, RAM > $2000) --\n");
	printf("-u[x] file[,a:addr,s:size,o:offset,x:entry]\n");
	printf("           Upload to address with size and file offset and "
	       "optionally execute.\n");
	printf("-x addr    Execute from address\n\n");

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
		  char **oFileName,
		  uint32_t *oBase,
		  uint32_t *oSize,
		  uint32_t *oOffset,
		  uint32_t *oExec)
{
	char *outName = NULL;
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
				}
			}

			*oReset = true;
		} else if (!strncmp(argv[i], "-u", 2)) {
			if (optLen > 2) {
				if (argv[i][3] != '\0') {
					usage();
					success = false;
					break;
				}

				if (argv[i][2] == 'x') {
					*oBoot = true;
				}
			}

			if (++i >= argc) {
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
		} else {
			usage();
			success = false;
			break;
		}
	}

	/* The user didn't ask us to do anything. Complain. */
	if (!*oReset && !outName && !*oBoot) {
		usage();
		success = false;
	}

	if (!success) {
		free(outName); outName = NULL;
		return false;
	}

	*oFileName = outName;
	return true;
}

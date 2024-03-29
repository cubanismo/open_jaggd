/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Author: James Jones
 */

/* Needed to get usleep() definition with glibc >= 2.19 */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>

#include <libusb-1.0/libusb.h>

#include "usberr.h"
#include "fileio.h"
#include "opts.h"

libusb_device_handle *IsJagGD(libusb_device *dev)
{
	static const char *GD_STR = "RetroHQ Jaguar GameDrive";

	struct libusb_device_descriptor desc;
	libusb_device_handle *hDev;
	char str[256];
	int strLen;
	int res;

	CHECKED_USB(libusb_get_device_descriptor(dev, &desc));

	if ((desc.bDeviceClass != 0xef) || /* LIBUSB_CLASS_MISCELLANEOUS */
	    (desc.bDeviceSubClass != 0x2) || /* ??? */
	    (desc.bDeviceProtocol != 0x1) || /* ??? */
	    (desc.idVendor != 0x03eb) || /* Atmel Corp. */
	    (desc.idProduct != 0x800e) || /* ??? */
	    (desc.iProduct == 0) /* Valid product string descriptor */) {
		return NULL;
	}

	res = libusb_open(dev, &hDev);

	if (res != LIBUSB_SUCCESS) {
		if (res == LIBUSB_ERROR_ACCESS) {
			printf("Insufficient permission to open USB device. "
			       "Try running as root.\n");
			return NULL;
		}

		DO_USB_ERR(res, "libusb_open");
	}

	CHECKED_USB_RES(strLen, libusb_get_string_descriptor_ascii(hDev,
			desc.iProduct, (unsigned char *)str, sizeof(str)));

	if ((strLen <= 0) || (strLen >= sizeof(str)) || strcmp(str, GD_STR)) {
		libusb_close(hDev);
		return NULL;
	}

	printf("Found Jaguar GameDrive - bus: %" PRIu8 " port: %" PRIu8
	       " device: %" PRIu8 "\n",
	       libusb_get_bus_number(dev),
	       libusb_get_port_number(dev),
	       libusb_get_device_address(dev));

	return hDev;
}

static void CloseGD(libusb_device_handle *hGD)
{
	if (hGD) {
		libusb_release_interface(hGD, 0);
		libusb_close(hGD); hGD = NULL;
	}
}

static libusb_device_handle *OpenGD(libusb_context *usbctx)
{
	libusb_device_handle *hGD = NULL;
	libusb_device **devs;
	ssize_t i, nDevs;
	int config;

	CHECKED_USB_RES(nDevs, libusb_get_device_list(usbctx, &devs));

	for (i = 0; i < nDevs; i++) {
		if ((hGD = IsJagGD(devs[i]))) {
			break;
		}

	}

	libusb_free_device_list(devs, 1 /* Do unref devices */);

	if (!hGD) {
		return NULL;
	}

	CHECKED_USB(libusb_get_configuration(hGD, &config));

	if (config == 0) {
		CHECKED_USB(libusb_set_configuration(hGD, 1));
	}

	/*
	 * Claim the erroneously-numbered "0" interface the JagGD uses for its
	 * control messages.
	 */
	CHECKED_USB(libusb_claim_interface(hGD, 0));

	return hGD;
}

static bool CheckMemRange(const char *addrType, uint32_t addr)
{
	static const uint32_t JAG_MIN_MEMORY = 0x2000U;
	static const uint32_t JAG_MAX_MEMORY = 0xE00000;

	if ((addr >= JAG_MIN_MEMORY) && (addr < JAG_MAX_MEMORY)) {
		return true;
	}

	fprintf(stderr, "%s address $%" PRIx32 " is out of range.\n",
		addrType, addr);
	fprintf(stderr, "Valid memory range: [$%" PRIx32 ", $%" PRIx32 ")\n",
		JAG_MIN_MEMORY, JAG_MAX_MEMORY);

	return false;
}

int main(int argc, char *argv[])
{
	libusb_context *usbctx = NULL;
	libusb_device_handle *hGD = NULL;
	JagFile *jf = NULL;
	FILE *fp = NULL;
	char *oFileName = NULL;
	char *oEepromName = NULL;
	char *oWriteFileName = NULL;
	uint32_t oBase = 0x0;
	uint32_t oSize = 0x0;
	uint32_t oOffset = 0xffffffffu;
	uint32_t oExec = 0x0;
	int transferSize;
	int exitCode = -1;
	bool oReset = false;
	bool oDebug = false;
	bool oBoot = false;
	bool oBootRom = false;
	uint8_t oEepromType = 0;
	static const uint32_t MAX_TRANSFER_SIZE = 16 * 1024;

	uint8_t reset[] = { 0x02, 0x00 };
	uint8_t writeFile[0x36] = {
		/* Total cmd size = 0x36, cmd = 0x05 */
		0x36, 0x05,

#define WF_OFF_FILE_NAME 0x02
		/* Destination file name = max 48 bytes, NUL terminated */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

#define WF_OFF_FILE_SIZE 0x32
		/* File size (Little endian) */
		0x00, 0x00, 0x00, 0x00
	};
	uint8_t eeprom[0x39] = {
		/* Total cmd size = 0x39, cmd = 0x02 */
		0x39, 0x02,

		/* Upload size, always zero */
		0x00, 0x00, 0x00, 0x00,

#define EEP_OFF_SIZE_AND_CMD 0x06
		/* server cmd size = 0x33, server cmd = 0x06 */
		0x33, 0x06,

#define EEP_OFF_EEPROM_TYPE 0x08
		/* 0 = 128b, 1 = 256b or 512b, 2 = 1024b or 2048b */
		0x00,

#define EEP_OFF_EEPROM_FNAME 0x09
		/* Filename on SD card, max 48 bytes, includes \0 terminator */
	};
	uint8_t uploadExec[] = { 0x14, 0x02,

#define UPEX_OFF_SIZE_LE 0x02
		/* Offset 0x2:
		 * Upload size, little-endian (LE), or 0 for exec-only */
		0x00, 0x00, 0x00, 0x00,

#define UPEX_OFF_MAGIC0 0x06
		/* Offset 0x6:
		 * ??? 0x0605 for exec-only, 0x0e04 for upload */
		0x06, 0x05,

#define UPEX_OFF_DST_OR_START 0x08
		/* Offset 0x8:
		 * Destination addr for upload, exec addr for exec-only, BE */
		0x00, 0x00, 0x00, 0x00,

#define UPEX_OFF_SIZE_BE_MAGIC1 0x0C
		/* Offset 0xC:
		 * Upload size, big-endian (BE), or 0x7a774a00 for exec-only */
		0x7a, 0x77, 0x4a, 0x00,

#define UPEX_OFF_START_MAGIC2 0x10
		/* Offset 0x10:
		 * Exec addr, BE, or 0x00008419 for exec-only */
		0x00, 0x00, 0x84, 0x19
	};

	printf("JagGD Version %d.%d.%d\n\n",
	       JAGGD_MAJOR, JAGGD_MINOR, JAGGD_MICRO);

	if (!ParseOptions(argc, argv, &oReset, &oDebug, &oBoot, &oBootRom,
			  &oFileName, &oBase, &oSize, &oOffset, &oExec,
			  &oEepromName, &oEepromType, &oWriteFileName)) {
		/* ParseOptions() prints usage on failure */
		return -1;
	}

	CHECKED_USB(libusb_init(&usbctx));

	hGD = OpenGD(usbctx);

	if (hGD == NULL) {
		fprintf(stderr, "Jaguar GameDrive not found\n");
		goto cleanup;
	}

	if (oReset) {
		printf("Reboot");
		if (oDebug) {
			/* Boot into the debug stub */
			reset[1] = 0x01;
			printf(" (Debug Console)\n");
		} else if (oBootRom) {
			/* Boot the currently loaded ROM from the Jaguar BIOS */
			reset[1] = 0x06;
			printf(" (ROM)\n");
		} else {
			/* Boot into the JagGD menu */
			reset[1] = 0x00;
			printf("\n");
		}

		/*
		 * Send a reset command over the control interface.
		 */
		CHECKED_USB(libusb_control_transfer(hGD,
					LIBUSB_REQUEST_TYPE_VENDOR |
					LIBUSB_RECIPIENT_INTERFACE,
					1, /* Request number */
					0, /* Value */
					0, /* Index: Specify interface 0 */
					reset, /* Data */
					sizeof(reset), /* Size */
					2000 /* 2 second timeout */));

		/* jaggd does this. Presumably it improves stability? */
		sleep(1);
		usleep(500000);
	}

	if (oEepromName) {
		eeprom[EEP_OFF_EEPROM_TYPE] = oEepromType;
		strncpy((char *)&eeprom[EEP_OFF_EEPROM_FNAME], oEepromName,
			(sizeof(eeprom) - EEP_OFF_EEPROM_FNAME) - 1);

		printf("Setting EEPROM file: '%s', %s bytes...", oEepromName,
		       (oEepromType == 0) ? "128" : (oEepromType == 1) ? "256/512" :
		       "1024/2048");
		fflush(stdout);

		/*
		 * Send enable EEPROM command over the control interface.
		 */
		CHECKED_USB(libusb_control_transfer(hGD,
					LIBUSB_REQUEST_TYPE_VENDOR |
					LIBUSB_RECIPIENT_INTERFACE,
					1, /* Request number */
					0, /* Value */
					0, /* Index: Specify interface 0 */
					eeprom, /* Data */
					sizeof(eeprom), /* Size */
					2000 /* 2 second timeout */));

		printf("OK\n");
	}

	if (oWriteFileName) {
		uint8_t bytes[MAX_TRANSFER_SIZE];
		const char *dstFileName;
		uint32_t size;
		uint32_t bytesUploaded = 0;
		uint32_t percent;
		bool first = true;

		fp = PrepFile(oWriteFileName, &dstFileName, &size);

		if (!fp) {
			/* PrepFile prints its own error messages */
			goto cleanup;
		}

		strncpy((char*)&writeFile[WF_OFF_FILE_NAME], dstFileName, 47);

		/*
		 * Use memcpy rather than a regular write, as the size field is
		 * not naturally aligned.
		 */
		memcpy(&writeFile[WF_OFF_FILE_SIZE], &size, sizeof(size));

		printf("WRITE FILE (%s)...", dstFileName);
		fflush(stdout);

		CHECKED_USB(libusb_control_transfer(hGD,
					LIBUSB_REQUEST_TYPE_VENDOR |
					LIBUSB_RECIPIENT_INTERFACE,
					1, /* Request number */
					0, /* Value */
					0, /* Index: Specify interface 0 */
					writeFile, /* Data */
					sizeof(writeFile), /* Size */
					2000 /* 2 second timeout */));

		while (bytesUploaded < size) {
			uint32_t bytesToTransfer = size - bytesUploaded;
			if (bytesToTransfer > MAX_TRANSFER_SIZE)
				bytesToTransfer = MAX_TRANSFER_SIZE;

			if (fread(&bytes[0], 1, bytesToTransfer, fp) !=
			    bytesToTransfer) {
				fprintf(stderr,
					"Failed to read data from local file\n");
				goto cleanup;
			}

			CHECKED_USB(libusb_bulk_transfer(hGD,
					LIBUSB_ENDPOINT_OUT |
					/* XXX 2 == Bulk out endpoint number */
					(LIBUSB_ENDPOINT_ADDRESS_MASK & 2),
					&bytes[0],
					bytesToTransfer,
					&transferSize,
					1000 * 60 * 2 /* 2 minute timeout */));
			bytesUploaded += transferSize;
			percent = ((uint64_t)bytesUploaded * 100u) / size;
			if (!first) printf("\b\b\b");
			else first = false;
			printf("%2" PRIu32 "%%", percent);
			fflush(stdout);
		}

		fclose(fp); fp = NULL;

		/* jaggd does this. Presumably it improves stability? */
		usleep(500000);
		printf("\nOK!\n");
	}

	if (oFileName) {
		jf = LoadFile(oFileName);

		if (!jf) {
			/* LoadFile prints its own error messages */
			goto cleanup;
		}

		if (oExec == 0x0) {
			oExec = jf->execAddr;
		}

		if (oBase != 0x0) {
			jf->baseAddr = oBase;
		}

		if (!CheckMemRange("Base upload", jf->baseAddr)) {
			goto cleanup;
		}

		if (oOffset != 0xffffffffu) {
			if (oOffset > jf->length) {
				fprintf(stderr, "Offset %" PRIu32
						"exceeds file length %zu\n",
					oOffset, jf->length);
				goto cleanup;
			}

			jf->offset = oOffset;
		}

		if (oSize != 0x0) {
			if ((oSize + jf->offset) > jf->length) {
				fprintf(stderr, "Size %" PRIu32 " + offset %"
					        PRId64 " exceeds file length "
						"%zu\n",
					oSize, (int64_t)jf->offset,
					jf->length);
				goto cleanup;
			}
			jf->dataSize = oSize;
		}
	}

	if (oBootRom) {
		oExec = 0xffffffff;
	} else if (oBoot && !CheckMemRange("Execution address", oExec)) {
		goto cleanup;
	}

	if (jf) {
		const uint32_t upSize = jf->dataSize;
		const uint32_t baseAddr = jf->baseAddr;
		const uint32_t execAddr = oBoot ? oExec : 0x0;


		uploadExec[UPEX_OFF_SIZE_LE+0] = (upSize      ) & 0xff;
		uploadExec[UPEX_OFF_SIZE_LE+1] = (upSize >>  8) & 0xff;
		uploadExec[UPEX_OFF_SIZE_LE+2] = (upSize >> 16) & 0xff;
		uploadExec[UPEX_OFF_SIZE_LE+3] = (upSize >> 24) & 0xff;

		uploadExec[UPEX_OFF_MAGIC0+0] = 0x0e;
		uploadExec[UPEX_OFF_MAGIC0+1] = 0x04;

		uploadExec[UPEX_OFF_DST_OR_START+0] = (baseAddr >> 24) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+1] = (baseAddr >> 16) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+2] = (baseAddr >>  8) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+3] = (baseAddr      ) & 0xff;

		uploadExec[UPEX_OFF_SIZE_BE_MAGIC1+0] = (upSize >> 24) & 0xff;
		uploadExec[UPEX_OFF_SIZE_BE_MAGIC1+1] = (upSize >> 16) & 0xff;
		uploadExec[UPEX_OFF_SIZE_BE_MAGIC1+2] = (upSize >>  8) & 0xff;
		uploadExec[UPEX_OFF_SIZE_BE_MAGIC1+3] = (upSize      ) & 0xff;

		uploadExec[UPEX_OFF_START_MAGIC2+0] = (execAddr >> 24) & 0xff;
		uploadExec[UPEX_OFF_START_MAGIC2+1] = (execAddr >> 16) & 0xff;
		uploadExec[UPEX_OFF_START_MAGIC2+2] = (execAddr >>  8) & 0xff;
		uploadExec[UPEX_OFF_START_MAGIC2+3] = (execAddr      ) & 0xff;

		printf("UPLOADING %s %zd BYTES TO $%" PRIx32, oFileName,
		       jf->dataSize, jf->baseAddr);
		if (jf->offset) {
			printf(" OFFSET $%" PRIx64, (int64_t)jf->offset);
		}

		if (oBootRom) {
			printf(" REBOOT");
		} else if (oExec != baseAddr) {
			printf(" ENTRY $%" PRIx32, oExec);
		}

		if (execAddr) {
			printf(" EXECUTE");
		}

		printf("...");
		fflush(stdout);
	} else if (oBoot) {
		uploadExec[UPEX_OFF_DST_OR_START+0] = (oExec >> 24) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+1] = (oExec >> 16) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+2] = (oExec >>  8) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+3] = (oExec      ) & 0xff;

		if (oBootRom) {
			printf("REBOOTING...");
		} else {
			printf("EXECUTING $%" PRIx32 "...", oExec);
		}
		fflush(stdout);
	}

	if (jf || oBoot) {
		uint32_t bytesUploaded = 0;
		uint32_t percent;
		bool first = true;

		/*
		 * Send an upload command over the control interface.
		 */
		CHECKED_USB(libusb_control_transfer(hGD,
					LIBUSB_REQUEST_TYPE_VENDOR |
					LIBUSB_RECIPIENT_INTERFACE,
					1, /* Request number */
					0, /* Value */
					0, /* Index: Specify interface 0 */
					uploadExec, /* Data */
					sizeof(uploadExec), /* Size */
					2000 /* 2 second timeout */));

		/*
		 * Send the data to the bulk endpoint
		 */
		while (jf && (bytesUploaded < jf->dataSize)) {
			static const int MAX_TRANSFER_SIZE = 16 * 1024;
			int bytesToTransfer = jf->dataSize - bytesUploaded;
			if (bytesToTransfer > MAX_TRANSFER_SIZE)
				bytesToTransfer = MAX_TRANSFER_SIZE;

			CHECKED_USB(libusb_bulk_transfer(hGD,
					LIBUSB_ENDPOINT_OUT |
					/* XXX 2 == Bulk out endpoint number */
					(LIBUSB_ENDPOINT_ADDRESS_MASK & 2),
					jf->buf + jf->offset + bytesUploaded,
					bytesToTransfer,
					&transferSize,
					1000 * 60 * 2 /* 2 minute timeout */));
			bytesUploaded += transferSize;
			percent = ((uint64_t)bytesUploaded * 100u) / jf->dataSize;
			if (!first) printf("\b\b\b");
			else first = false;
			printf("%2" PRIu32 "%%", percent);
			fflush(stdout);
		}

		printf("\nOK!\n");
	}

	/* Success */
	exitCode = 0;

cleanup:
	/* Close the write-to-memory-card file */
	if (fp) fclose(fp);

	/* Free file data */
	FreeFile(jf);

	/* Shut down the device */
	CloseGD(hGD);

	/* Shut down libusb */
	libusb_exit(usbctx); usbctx = NULL;

	free(oWriteFileName); oWriteFileName = NULL;
	free(oEepromName); oEepromName = NULL;
	free(oFileName); oFileName = NULL;

	return exitCode;
}

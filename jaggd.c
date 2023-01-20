/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Author: James Jones
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

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
	char *oFileName = NULL;
	uint32_t oBase = 0x0;
	uint32_t oSize = 0x0;
	uint32_t oOffset = 0xffffffffu;
	uint32_t oExec = 0x0;
	int bytesUploaded = 0;
	int transferSize;
	int exitCode = -1;
	int oReset = false;
	bool oDebug = false;
	int oBoot = false;

	uint8_t reset[] = { 0x02, 0x00 };
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

	if (!ParseOptions(argc, argv, &oReset, &oDebug, &oBoot,
			  &oFileName, &oBase, &oSize, &oOffset, &oExec)) {
		/* ParseOptions() prints usage on failure */
		return -1;
	}

	CHECKED_USB(libusb_init(&usbctx));

	hGD = OpenGD(usbctx);

	if (hGD == NULL) {
		fprintf(stderr, "Jaguar GameDrive not found\n");
		goto cleanup;
	}

	if (oReset > ECmd_ResetNone) {
		printf("Reboot");
                reset[1] = oReset;
		if (oReset == ECmd_ResetDebug) {
                  /* Boot into the debug stub */
                  printf(" (Debug Console)\n");
		} else if ( oReset == ECmd_ResetRom ){
                  printf(" (ROM)\n");
                } else {
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

		if (oExec != 0xffffffff && !CheckMemRange("Base upload", oExec)) {
			goto cleanup;
		}

		if (oOffset != 0xffffffffu) {
			if (oOffset > jf->length) {
				fprintf(stderr, "Offset %" PRIu32
						"exceeds file length %zd\n",
					oOffset, jf->length);
				goto cleanup;
			}

			jf->offset = oOffset;
		}

		if (oSize != 0x0) {
			if ((oSize + jf->offset) > jf->length) {
				fprintf(stderr, "Size %" PRIu32 " + offset %lld "
						"exceeds file length "
						"%zd\n",
					oSize, jf->offset, jf->length);
				goto cleanup;
			}
			jf->dataSize = oSize;
		}
	}

	if (oBoot && oExec != 0xffffffff && !CheckMemRange("Execution address", oExec)) {
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
			printf(" OFFSET $%llx", jf->offset);
		}

		if (oExec != baseAddr) {
                  if ( oExec == 0xffffffff ){
                    printf(" REBOOT");
                  } else {
                    printf(" ENTRY $%" PRIx32, oExec);
                  }
		}

		if (execAddr) {
			printf(" EXECUTE");
		}
		printf("\n");
	} else if (oBoot) {
		uploadExec[UPEX_OFF_DST_OR_START+0] = (oExec >> 24) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+1] = (oExec >> 16) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+2] = (oExec >>  8) & 0xff;
		uploadExec[UPEX_OFF_DST_OR_START+3] = (oExec      ) & 0xff;

		printf("EXECUTING $%" PRIx32 "...", oExec);
		fflush(stdout);
	}

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
        if ( jf ) printf("Uploading ...");
	while (jf && (bytesUploaded < jf->dataSize)) {
		CHECKED_USB(libusb_bulk_transfer(hGD,
					LIBUSB_ENDPOINT_OUT |
					/* XXX 2 == Bulk out endpoint number */
					(LIBUSB_ENDPOINT_ADDRESS_MASK & 2),
					jf->buf + jf->offset + bytesUploaded,
					jf->dataSize - bytesUploaded,
					&transferSize,
					1000 * 60 * 2 /* 2 minute timeout */));
		bytesUploaded += transferSize;
                printf(".");
                fflush(stdout);
	}
        printf("\n");
	if (jf || oBoot) {
		printf("OK!\n");
	}
	/* Success */
	exitCode = 0;

cleanup:
	/* Free file data */
	FreeFile(jf);

	/* Shut down the device */
	CloseGD(hGD);

	/* Shut down libusb */
	libusb_exit(usbctx); usbctx = NULL;

	free(oFileName); oFileName = NULL;

	return exitCode;
}

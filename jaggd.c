#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include <libusb-1.0/libusb.h>

#include "usberr.h"
#include "fileio.h"

libusb_device_handle *is_jaggd_dev(libusb_device *dev)
{
	struct libusb_device_descriptor desc;
	libusb_device_handle *hDev;
	char prodStr[256];
	int strLen;

	CHECKED_USB(libusb_get_device_descriptor(dev, &desc));

	if ((desc.bDeviceClass == 0xef) && /* LIBUSB_CLASS_MISCELLANEOUS */
	    (desc.bDeviceSubClass == 0x2) && /* ??? */
	    (desc.bDeviceProtocol == 0x1) && /* ??? */
	    (desc.idVendor == 0x03eb) && /* Atmel Corp. */
	    (desc.idProduct == 0x800e) && /* ??? */
	    (desc.iProduct != 0) /* Valid product string descriptor */) {
		int res = libusb_open(dev, &hDev);

		if (res != 0) {
			if (res == LIBUSB_ERROR_ACCESS) {
				printf("No permission to open suspected JagGD "
				       "device. Try running as root.\n");
				return NULL;
			}

			DO_USB_ERR(res, "libusb_open");
		}

		CHECKED_USB_RES(strLen, libusb_get_string_descriptor_ascii(hDev,
					desc.iProduct,
					(unsigned char *)prodStr,
					sizeof(prodStr)));

		if (strLen > 0 && strLen < 256 &&
			!strcmp(prodStr, "RetroHQ Jaguar GameDrive")) {
			printf("Found product: (%" PRIu8 ":%" PRIu8 ":%" PRIu8
			       ") %s\n",
			       libusb_get_bus_number(dev),
			       libusb_get_port_number(dev),
			       libusb_get_device_address(dev),
			       prodStr);
			return hDev;
		}
		libusb_close(hDev);
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	libusb_context *usbctx = NULL;
	libusb_device **devs;
	libusb_device_handle *hGD = NULL;
	JagFile *jf = NULL;
	const char *upFileName = "JAGLION.COF"; /* XXX hard-coded file name */
	ssize_t i, nDevs;
	int bytesUploaded = 0;
	int transferSize;
	int res;
	int exitCode = -1;
	bool doReset = false;

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

	CHECKED_USB(libusb_init(&usbctx));

	CHECKED_USB_RES(nDevs, libusb_get_device_list(usbctx, &devs));

	if (nDevs < 0) {
		fprintf(stderr, "!! No USB devices found\n");
		abort();
	}

	for (i = 0; i < nDevs; i++) {
		if ((hGD = is_jaggd_dev(devs[i]))) {
			break;
		}

	}

	libusb_free_device_list(devs, 1 /* Do unref devices */);

	if (!hGD) {
		return -1;
	}

	res = libusb_set_configuration(hGD, 1);

	/* The device only supports one configuration, so failure here is OK in
	 * the following cases:
	 *
	 * LIBUSB_ERROR_NOT_SUPPORTED:
	 *   Can't configure from userspace on this OS. That's OK, we'll assume
	 *   the OS already set the one valid configuration in the kernel.
	 *
	 * LIBUSB_ERROR_BUSY:
	 *   The OS already has a driver bound to at least one interface on the
	 *   device and can't reconfigure it without messing that driver up.
	 *   Again, that's OK, the OS would have had to set the one valid
	 *   configuration to initialize a driver on the device.
	 */
	if ((res != LIBUSB_SUCCESS) &&
	    (res != LIBUSB_ERROR_NOT_SUPPORTED) &&
	    (res != LIBUSB_ERROR_BUSY)) {
		DO_USB_ERR(res, "libusb_set_configuration");
	}

	/*
	 * Claim the erroneously-numbered "0" interface the JagGD uses for its
	 * control messages.
	 */
	CHECKED_USB(libusb_claim_interface(hGD, 0));

	if (argc > 1) {
		reset[1] = 0x01;
	} else {
		reset[1] = 0x00;
	}

	if (doReset) {
		printf("Reboot%s\n",
		       (reset[1] == 0x01) ? " (Debug Console)" : "");
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

		/* XXX reconnect and carry on */
		goto cleanup;
	}

	jf = LoadFile(upFileName);

	if (!jf) {
		/* LoadFile prints its own error messages */
		goto cleanup;
	}

	if (jf) {
		const uint32_t upSize = jf->dataSize;
		const uint32_t baseAddr = jf->baseAddr;
		const uint32_t execAddr = jf->execAddr;

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

		printf("UPLOADING %s %zd BYTES TO $%" PRIx32, upFileName,
		       jf->dataSize, jf->baseAddr);
		if (jf->offset) {
			printf(" OFFSET $%zx", jf->offset);
		}

		if (execAddr != baseAddr) {
			printf(" ENTRY $%" PRIx32, jf->execAddr);
		}

		if (execAddr) {
			printf(" EXECUTE");
		}

		printf("\n");
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
	while (bytesUploaded < jf->dataSize) {
		CHECKED_USB(libusb_bulk_transfer(hGD,
					LIBUSB_ENDPOINT_OUT |
					/* XXX 2 == Bulk out endpoint number */
					(LIBUSB_ENDPOINT_ADDRESS_MASK & 2),
					jf->buf + jf->offset + bytesUploaded,
					jf->dataSize - bytesUploaded,
					&transferSize,
					1000 * 60 * 5 /* 5 minute timeout */));
		bytesUploaded += transferSize;
	}

	if (jf) {
		printf("OK!\n");
	}

	/* Success */
	exitCode = 0;

cleanup:
	/* Free file data */
	FreeFile(jf);

	/* Shut down the device */
	libusb_release_interface(hGD, 0);
	libusb_close(hGD); hGD = NULL;

	/* Shut down libusb */
	libusb_exit(usbctx); usbctx = NULL;

	return exitCode;
}

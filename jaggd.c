#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <libusb-1.0/libusb.h>

#include "usberr.h"

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
	ssize_t i, nDevs;

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

	return 0;
}

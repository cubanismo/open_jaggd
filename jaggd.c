#include <stdio.h>
#include <stdlib.h>

#include <libusb-1.0/libusb.h>

static libusb_context *usbctx = NULL;

#define CHECKED_USB(mycall) do {				\
	int usberr = (mycall);					\
	if (usberr != 0) {					\
		fprintf(stderr, "!! libusb(%s) err: %s\n",	\
			#mycall, libusb_error_name(usberr));	\
		abort();					\
	}							\
} while (0)

int main(int argc, char *argv[])
{
	CHECKED_USB(libusb_init(&usbctx));

	return 0;
}

#include <stdio.h>

#include <libusb-1.0/libusb.h>

#define USB_ERR_HELPER(myerr, mycallstr, file, line) do {	\
	fprintf(stderr, "!! %s:%d: libusb(%s) err: %s\n",	\
		(file), (line), mycallstr,			\
		libusb_error_name((int)(myerr)));		\
		abort();					\
} while (0)

#define DO_USB_ERR(myerr, message) \
	USB_ERR_HELPER(myerr, message, __FILE__, __LINE__)

#define CHECKED_USB_HELPER(myres, mycall, mycallstr, file, line) do {	\
	(myres) = (mycall);						\
	if ((myres) < 0) {						\
		USB_ERR_HELPER((myres), (mycallstr), (file), (line));	\
	}								\
} while (0)

#define CHECKED_USB(mycall) do {			\
	int usberr;					\
	CHECKED_USB_HELPER(usberr, mycall, #mycall,	\
			   __FILE__, __LINE__);		\
} while (0)

#define CHECKED_USB_RES(res, mycall) do {		\
	CHECKED_USB_HELPER((res), mycall, #mycall,	\
			   __FILE__, __LINE__);		\
} while (0)

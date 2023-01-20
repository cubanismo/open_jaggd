###############################################################################
#
# SPDX-License-Identifier: CC0-1.0
#
# Author: James Jones
#
###############################################################################

# Adjust these when making new releases
JAGGD_MAJOR = 1
JAGGD_MINOR = 0
JAGGD_MICRO = 1

CDEFS ?=
CPPFLAGS ?=
CFLAGS ?= -O2 -Wall
#-Werror
LDLIBS ?=

CDEFS += -DJAGGD_MAJOR=$(JAGGD_MAJOR) \
	 -DJAGGD_MINOR=$(JAGGD_MINOR) \
	 -DJAGGD_MICRO=$(JAGGD_MICRO)

CPPFLAGS += $(CDEFS)

OBJECTS = jaggd.o fileio.o opts.o
DEPS = $(patsubst %.o,.%.dep,$(OBJECTS))
PROGS = jaggd

all: $(PROGS)

.%.dep: %.c
	$(CC) -MM $^ -o $@

jaggd: $(OBJECTS)
jaggd: LDLIBS += -lusb-1.0

clean:
	rm -f $(OBJECTS) $(PROGS)

include $(DEPS)

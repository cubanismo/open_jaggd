CFLAGS = -O2 -Wall -Werror
LDLIBS =

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

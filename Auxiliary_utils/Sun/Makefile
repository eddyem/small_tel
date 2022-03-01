# run `make DEF=...` to add extra defines
PROGRAM = noon
LDFLAGS := -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,--discard-all
LDFLAGS += -lnova
SRCS := $(wildcard *.c)
DEFINES := $(DEF) -D_GNU_SOURCE -D_XOPEN_SOURCE=1111
CFLAGS += -O2 -Wno-trampolines -std=gnu99
CC = gcc


LINKS := sunrise sunset

all : $(LINKS)

$(LINKS) : $(PROGRAM)
	@echo "make links"
	ln -s $< $@ || true

$(PROGRAM): sun.c
	@echo -e "\t\tCC $<"
	$(CC) $(LDFLAGS) $(CFLAGS) $(DEFINES) -o $@ $<

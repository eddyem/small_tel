# run `make DEF=...` to add extra defines
PROGRAM := weatherdaemon
LDFLAGS := -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,--discard-all
LDFLAGS += -lusefull_macros
SRCS := $(wildcard *.c)
DEFINES := $(DEF) -D_GNU_SOURCE -D_XOPEN_SOURCE=1111
OBJDIR := mk
CFLAGS += -O2 -Wall -Wextra -Wno-trampolines
OBJS := $(addprefix $(OBJDIR)/, $(SRCS:%.c=%.o))
DEPS := $(OBJS:.o=.d)
TARGFILE := $(OBJDIR)/TARGET
CC = gcc

ifeq ($(shell test -e $(TARGFILE) && echo -n yes),yes)
	TARGET := $(file < $(TARGFILE))
else
	TARGET := RELEASE
endif

ifeq ($(TARGET), DEBUG)
	.DEFAULT_GOAL := debug
endif

release: CFLAGS += -flto
release: LDFLAGS += -flto
release: $(PROGRAM)

debug: CFLAGS += -DEBUG -Werror
debug: TARGET := DEBUG
debug: $(PROGRAM)

$(TARGFILE): $(OBJDIR)
	@echo -e "\t\tTARGET: $(TARGET)"
	@echo "$(TARGET)" > $(TARGFILE)

$(PROGRAM) : $(TARGFILE) $(OBJS)
	@echo -e "\t\tLD $(PROGRAM)"
	$(CC) $(LDFLAGS) $(OBJS) -o $(PROGRAM)

$(OBJDIR):
	@mkdir $(OBJDIR)

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif

$(OBJDIR)/%.o: %.c
	@echo -e "\t\tCC $<"
	$(CC) -MD -c $(LDFLAGS) $(CFLAGS) $(DEFINES) -o $@ $<

clean:
	@echo -e "\t\tCLEAN"
	@rm -rf $(OBJDIR) 2>/dev/null || true

xclean: clean
	@rm -f $(PROGRAM)

.PHONY: clean xclean

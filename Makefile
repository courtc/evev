PREFIX ?= /usr/local
PREFIX_ETC ?= $(PREFIX)/etc
PREFIX_BIN ?= $(PREFIX)/bin

deffn = $(foreach v,$(1),-D$v=\"$($v)\")
DEFINES := $(call deffn,PREFIX PREFIX_ETC PREFIX_BIN)

OPT ?= -O2
CFLAGS ?= -Wall $(OPT)
CFLAGS += $(DEFINES)
LDFLAGS ?= $(OPT)

SCFLAGS := \
	-gcc-base-dir $(shell $(CC) --print-file-name=) \
	-Wno-non-pointer-null

out := out
src_to_obj = $(patsubst %.c,$(out)/obj/%.o,$(filter %.c,$(1)))
src_to_dep = $(patsubst %.c,$(out)/dep/%.d,$(filter %.c,$(1)))

srcs := \
	src/expr.c \
	src/context.c \
	src/parser.c \
	src/evev.c \
	src/tables.c \

objs := $(call src_to_obj,$(srcs))
deps := $(call src_to_dep,$(srcs))

all: evev

evev: $(objs)
	@echo "LD	$@"
	@$(CC) -o $@ $(LDFLAGS) $^ $($@-LDFLAGS)

$(call src_to_obj,%.c): %.c
ifneq ($C,)
	@echo "CHECK	$<"
	@sparse $< $(CFLAGS) $($<-CFLAGS) $(SCFLAGS)
endif
	@echo "CC	$<"
	@$(CC) -MM -MF $(call src_to_dep,$<) -MP \
		-MT "$@ $(call src_to_dep,$<)" $(CFLAGS) $($<-CFLAGS) $<
	@$(CC) -o $@ -c $< $(CFLAGS) $($<-CFLAGS)

src/tables.c: /usr/include/linux/input-event-codes.h input-ev.sh
	@echo "GEN	$@"
	@./input-ev.sh $< > $@
src/tables.c-CFLAGS := -Wno-unused

clean:
	$(RM) -r $(out) evev src/tables.c

install: evev
	install -d $(DESTDIR)$(PREFIX_BIN)
	install -d $(DESTDIR)$(PREFIX_ETC)/evev
	install -m 755 evev $(DESTDIR)$(PREFIX_BIN)

uninstall:
	-rmdir --ignore-fail-on-non-empty $(DESTDIR)$(PREFIX_ETC)/evev
	rm -f $(DESTDIR)$(PREFIX_BIN)/evev

$(objs) $(deps): Makefile

.PHONY: clean install uninstall

ifneq ("$(MAKECMDGOALS)","clean")
cmd-goal-1 := $(shell mkdir -p $(sort $(dir $(objs) $(deps))))
-include $(deps)
endif

OPT := -g
CFLAGS := -Wall $(OPT)
LDFLAGS := $(OPT)

objs := \
	src/expr.o \
	src/context.o \
	src/parser.o \
	src/evev.o \
	src/tables.o \

all: evev

evev: $(objs)
	@echo "LD	$@"
	@$(CC) -o $@ $(LDFLAGS) $^

%.o: %.c
	@echo "CC	$<"
	@$(CC) -o $@ -c $< $(CFLAGS) $($(@)-CFLAGS)

clean:
	rm -f $(objs) evev src/tables.c

src/tables.c: /usr/include/linux/input-event-codes.h input-ev.sh
	@echo "GEN	$@"
	@./input-ev.sh $< > $@
src/tables.o-CFLAGS := -Wno-unused

.PHONY: clean

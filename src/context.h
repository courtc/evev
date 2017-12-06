#ifndef __CONTEXT_H_
#define __CONTEXT_H_

#include "types.h"

struct expr;
struct binding {
	struct expr *expr;
	int state;
	struct binding *next;
	char command[0];
};

struct evstate {
	unsigned int typecode;
	int value;

	struct binding **listeners;
	unsigned int nlisteners;
};

struct context {
	struct evstate *states;
	unsigned int nstates;
	struct binding *bindings;

	struct expr **durations;
	unsigned int ndurations;
};

int ctx_init(struct context *ctx, struct binding *bindings);

int ctx_input_event(struct context *ctx,
		int (*run)(const char *command),
		unsigned int typecode, int value, u64 now);

int ctx_timeout(struct context *ctx, int (*run)(const char *command), u64 now);

#endif

// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2017 Courtney Cavin

#include <stdlib.h>
#include <string.h>

#include "context.h"
#include "expr.h"

static int ctx_state_cmp(const void *a, const void *b)
{
	const struct evstate *sa = a;
	const struct evstate *sb = b;

	return sa->typecode - sb->typecode;
}

static void ctx_init_expr_pass2(struct context *ctx, struct expr *e)
{
	switch (e->type) {
	case EXPR_OR:
	case EXPR_XOR:
	case EXPR_AND:
		ctx_init_expr_pass2(ctx, e->binop.left);
		ctx_init_expr_pass2(ctx, e->binop.right);
		break;
	case EXPR_NOT:
		ctx_init_expr_pass2(ctx, e->not);
		break;
	case EXPR_DUR:
		ctx_init_expr_pass2(ctx, e->dur.expr);
		break;
	case EXPR_PRIMARY: {
		struct expr_match m;
		unsigned int index;

		m = e->primary;

		e->type = EXPR_CINFO;
		e->cinfo.value = m.value;
		e->cinfo.cmp = m.cmp;

		index = ctx->nstates;

		for (unsigned int i = 0; i < ctx->nstates; ++i) {
			if (ctx->states[i].typecode == m.lookup) {
				index = i;
				break;
			}
		}
		e->cinfo.lookup = index;

		} break;
	case EXPR_CINFO:
		break;
	}
}

static void ctx_init_expr_pass1(struct context *ctx,
		struct binding *binding, struct expr *e)
{
	switch (e->type) {
	case EXPR_OR:
	case EXPR_XOR:
	case EXPR_AND:
		ctx_init_expr_pass1(ctx, binding, e->binop.left);
		ctx_init_expr_pass1(ctx, binding, e->binop.right);
		break;
	case EXPR_NOT:
		ctx_init_expr_pass1(ctx, binding, e->not);
		break;
	case EXPR_DUR:
		++ctx->ndurations;
		ctx_init_expr_pass1(ctx, binding, e->dur.expr);
		break;
	case EXPR_PRIMARY: {
		struct evstate *evs;
		unsigned int index;

		index = ctx->nstates;

		for (unsigned int i = 0; i < ctx->nstates; ++i) {
			if (ctx->states[i].typecode == e->primary.lookup) {
				index = i;
				break;
			}
		}
		if (index == ctx->nstates) {
			ctx->states = realloc(ctx->states,
					sizeof(*ctx->states) * ++ctx->nstates);
			memset(&ctx->states[ctx->nstates - 1], 0,
					sizeof(*ctx->states));
			ctx->states[ctx->nstates - 1].typecode = e->primary.lookup;
		}

		evs = &ctx->states[index];
		evs->listeners = realloc(evs->listeners,
				sizeof(binding) * ++evs->nlisteners);
		evs->listeners[evs->nlisteners - 1] = binding;
		} break;
	case EXPR_CINFO:
		break;
	}
}

int ctx_init(struct context *ctx, struct binding *bindings)
{
	ctx->durations = NULL;
	ctx->states = NULL;
	ctx->ndurations = 0;
	ctx->bindings = bindings;
	ctx->nstates = 0;

	for (struct binding *b = bindings; b; b = b->next)
		ctx_init_expr_pass1(ctx, b, b->expr);

	qsort(ctx->states, ctx->nstates, sizeof(*ctx->states), ctx_state_cmp);

	for (struct binding *b = bindings; b; b = b->next)
		ctx_init_expr_pass2(ctx, b->expr);

	ctx->durations = calloc(ctx->ndurations, sizeof(*ctx->durations));
	if (ctx == NULL) {
		free(ctx->states);
		return -1;
	}
	ctx->ndurations = 0;

	return 0;
}

static int ctx_value(struct context *ctx, unsigned int idx)
{
	return ctx->states[idx].value;
}

static void ctx_dur_remove(struct context *ctx, struct expr *e)
{
	for (unsigned int i = 0; i < ctx->ndurations; ++i) {
		if (ctx->durations[i] == e) {
			ctx->durations[i] = NULL;
			break;
		}
	}

	while (ctx->ndurations > 0 &&
			ctx->durations[ctx->ndurations - 1] == NULL)
		--ctx->ndurations;
}

static int ctx_expr_eval(struct context *ctx, struct expr *e, u64 now)
{
	int rc;

	switch (e->type) {
	case EXPR_OR:
		return ctx_expr_eval(ctx, e->or.left, now) |
			ctx_expr_eval(ctx, e->or.right, now);
	case EXPR_XOR:
		return ctx_expr_eval(ctx, e->xor.left, now) ^
			ctx_expr_eval(ctx, e->xor.right, now);
	case EXPR_AND:
		return ctx_expr_eval(ctx, e->and.left, now) &
			ctx_expr_eval(ctx, e->and.right, now);
	case EXPR_NOT:
		return !ctx_expr_eval(ctx, e->not, now);
	case EXPR_DUR:
		rc = ctx_expr_eval(ctx, e->dur.expr, now);
		if (rc) {
			if (e->dur.end == 0) {
				e->dur.end = now + e->dur.duration;
				ctx->durations[ctx->ndurations++] = e;
			} else if (now >= e->dur.end) {
				ctx_dur_remove(ctx, e);
				return 1;
			}
		} else if (e->dur.end != 0) {
			ctx_dur_remove(ctx, e);
			e->dur.end = 0;
		}
		return 0;
	case EXPR_PRIMARY:
	case EXPR_CINFO:
		return expr_cmp(&e->cinfo, ctx_value(ctx, e->cinfo.lookup));
	}

	return 0;
}

static int ctx_pollwait(struct context *ctx, u64 now)
{
	unsigned int wait = -1;

	for (unsigned int i = 0; i < ctx->ndurations; ++i) {
		struct expr *e = ctx->durations[i];
		unsigned int left;

		if (e == NULL)
			continue;

		if (e->dur.end <= now)
			return 0;

		left = e->dur.end - now;
		if (left < wait)
			wait = left;
	}

	return wait;
}

static void ctx_binding_eval(struct context *ctx, struct binding *b,
		int (*run)(const char *command), u64 now)
{
	int rc;

	rc = ctx_expr_eval(ctx, b->expr, now);

	if (rc == b->state)
		return;

	if (rc)
		run(b->command);
	b->state = rc;
}

int ctx_timeout(struct context *ctx, int (*run)(const char *command), u64 now)
{
	for (struct binding *b = ctx->bindings; b; b = b->next)
		ctx_binding_eval(ctx, b, run, now);

	return ctx_pollwait(ctx, now);
}

int ctx_input_event(struct context *ctx,
		int (*run)(const char *command),
		unsigned int typecode, int value, u64 now)
{
	unsigned int h = ctx->nstates;
	unsigned int l = 0;
	struct evstate *e;

	while (l < h) {
		unsigned int m = l + ((h - l) >> 1);
		int c;

		e = &ctx->states[m];
		c = typecode - e->typecode;

		if (c < 0)
			h = m;
		else if (c > 0)
			l = m + 1;
		else
			break;
	}


	if (l >= h || e->value == value)
		return ctx_pollwait(ctx, now);

	e->value = value;
	for (unsigned int i = 0; i < e->nlisteners; ++i) {
		struct binding *b = e->listeners[i];

		ctx_binding_eval(ctx, b, run, now);
	}

	return ctx_pollwait(ctx, now);
}

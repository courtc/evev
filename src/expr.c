// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2017 Courtney Cavin

#include <stdlib.h>
#include "expr.h"

struct expr *expr_new(void)
{
	struct expr *e;

	e = calloc(1, sizeof(*e));
	if (e == NULL)
		return NULL;

	return e;
}

int expr_cmp(struct expr_match *m, int value)
{
	switch (m->cmp) {
	case EXPR_EQ: return value == m->value;
	case EXPR_NE: return value != m->value;
	case EXPR_LE: return value <= m->value;
	case EXPR_GE: return value >= m->value;
	case EXPR_LT: return value < m->value;
	case EXPR_GT: return value > m->value;
	}

	return 0;
}

void expr_free(struct expr *e)
{
	switch (e->type) {
	case EXPR_OR:
	case EXPR_XOR:
	case EXPR_AND:
		expr_free(e->binop.left);
		expr_free(e->binop.right);
		break;
	case EXPR_NOT:
		expr_free(e->not);
		break;
	case EXPR_DUR:
		expr_free(e->dur.expr);
		break;
	case EXPR_PRIMARY:
	case EXPR_CINFO:
		break;
	}
	free(e);
}

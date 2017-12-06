// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2017 Courtney Cavin

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "context.h"
#include "parser.h"
#include "tables.h"
#include "types.h"
#include "expr.h"

static int psr_comment(const char **pdata)
{
	const char *data = *pdata;

	if (data[0] != '#')
		return 0;

	while (*data && *data != '\n')
		++data;

	if (*data)
		++data;

	*pdata = data;

	return 1;
}

static void psr_whitespace(const char **pdata)
{
	const char *data = *pdata;

	do {
		while (isspace(*data))
			++data;
	} while (psr_comment(&data));

	*pdata = data;
}

static int psr_consume_char(const char **pdata, int ch)
{
	const char *data = *pdata;

	if (data[0] != ch)
		return -1;

	++data;
	psr_whitespace(&data);
	*pdata = data;

	return 0;
}

static struct expr *psr_any_of(const char **pdata,
		struct expr *(*fns[])(const char **pdata), int nfns)
{
	struct expr *c;

	for (int i = 0; i < nfns; ++i) {
		c = fns[i](pdata);
		if (c != NULL)
			return c;
	}

	return NULL;
}

static struct expr *psr_binop_expr(enum expr_type t,
		struct expr *l, struct expr *r)
{
	struct expr *c;

	c = expr_new();
	if (c == NULL)
		return NULL;

	c->type = t;

	c->binop.left = l;
	c->binop.right = r;

	return c;
}

static struct expr *psr_seq(const char **pdata, int ch,
		enum expr_type type, struct expr *(* fn)(const char **pdata))
{
	struct expr *c;

	c = fn(pdata);
	while (c) {
		const char *data = *pdata;
		struct expr *r;

		if (psr_consume_char(&data, ch))
			break;
		r = fn(&data);
		if (r) {
			*pdata = data;
			c = psr_binop_expr(type, c, r);
		}
	}

	return c;
}

static unsigned int psr_duration(const char **pdata)
{
	const char *data = *pdata;
	unsigned int dur;
	char *ep;

	if (psr_consume_char(&data, '['))
		return 0;

	dur = strtoul(data, &ep, 10);
	data = ep;

	if (!strncmp(data, "s", 1)) {
		dur *= 1000;
		data += 1;
	} else if (!strncmp(data, "ms", 2)) {
		data += 2;
	}

	if (psr_consume_char(&data, ']'))
		return 0;

	*pdata = data;

	return dur;
}

static struct expr *psr_expr_any(const char **pdata);

static struct expr *psr_expr_group(const char **pdata)
{
	const char *data = *pdata;
	struct expr *c;

	if (psr_consume_char(&data, '('))
		return NULL;

	c = psr_expr_any(&data);
	if (c == NULL)
		return NULL;

	if (psr_consume_char(&data, ')')) {
		expr_free(c);
		return NULL;
	}

	*pdata = data;

	return c;
}

static int psr_expr_ctab_lookup(const char **pdata, struct expr_match *m)
{
	const char *data = *pdata;
	unsigned int h = codetab_sz;
	unsigned int l = 0;
	unsigned int mlen = 0;
	const struct code_entry *e;
	char buf[64];

	while (mlen < sizeof(buf) - 1 && data[mlen] &&
			(data[mlen] == '_' || isalnum(data[mlen]))) {
		buf[mlen] = data[mlen];
		++mlen;
	}
	buf[mlen] = 0;

	while (l < h) {
		unsigned int m = l + ((h - l) >> 1);
		int c;

		e = &codetab[m];
		c = strcmp(buf, e->name);

		if (c < 0)
			h = m;
		else if (c > 0)
			l = m + 1;
		else
			break;
	}
	if (l >= h)
		return -1;

	m->lookup = expr_typecode(e->type, e->code);
	data += strlen(e->name);
	psr_whitespace(&data);
	if (!psr_consume_char(&data, ':')) {
		char *ep;

		if (!strncmp(data, "eq", 2)) {
			m->cmp = EXPR_EQ;
			data += 2;
		} else if (!strncmp(data, "ne", 2)) {
			m->cmp = EXPR_NE;
			data += 2;
		} else if (!strncmp(data, "lt", 2)) {
			m->cmp = EXPR_LT;
			data += 2;
		} else if (!strncmp(data, "gt", 2)) {
			m->cmp = EXPR_GT;
			data += 2;
		} else if (!strncmp(data, "le", 2)) {
			m->cmp = EXPR_LE;
			data += 2;
		} else if (!strncmp(data, "ge", 2)) {
			m->cmp = EXPR_GE;
			data += 2;
		} else {
			m->cmp = EXPR_EQ;
		}

		m->value = strtoul(data, &ep, 0);
		data = ep;
		psr_whitespace(&data);
	} else {
		m->cmp = EXPR_EQ;
		m->value = 1;
	}

	*pdata = data;

	return 0;
}

static struct expr *psr_expr_event(const char **pdata)
{
	const char *data = *pdata;
	struct expr_match m;
	struct expr *c;

	if (psr_expr_ctab_lookup(&data, &m))
		return NULL;

	c = expr_new();
	if (c == NULL)
		return NULL;

	c->type = EXPR_PRIMARY;
	c->primary = m;

	psr_whitespace(&data);

	*pdata = data;

	return c;
}

static struct expr *psr_expr_postfix(const char **pdata)
{
	struct expr *(*opts[])(const char **) = {
		psr_expr_group, psr_expr_event,
	};
	const char *data = *pdata;
	unsigned int dur;
	struct expr *e;

	e = psr_any_of(&data, opts, ARRAY_SIZE(opts));
	if (e == NULL)
		return NULL;

	dur = psr_duration(&data);
	if (dur != 0) {
		struct expr *de;

		de = expr_new();
		if (de == NULL)
			return NULL;

		de->type = EXPR_DUR;
		de->dur.duration = dur;
		de->dur.expr = e;
		e = de;
	}

	*pdata = data;

	return e;
}

static struct expr *psr_expr_not(const char **pdata);

static struct expr *psr_expr_primary(const char **pdata)
{
	struct expr *(*opts[])(const char **) = {
		psr_expr_not, psr_expr_postfix,
	};
	return psr_any_of(pdata, opts, ARRAY_SIZE(opts));
}

static struct expr *psr_expr_not(const char **pdata)
{
	const char *data = *pdata;
	struct expr *c, *p;

	if (psr_consume_char(&data, '!'))
		return NULL;

	c = psr_expr_primary(&data);
	if (c == NULL)
		return NULL;

	p = expr_new();
	if (p == NULL)
		return NULL;

	p->type = EXPR_NOT;
	p->not = c;

	*pdata = data;

	return p;
}

static struct expr *psr_expr_and(const char **pdata)
{
	return psr_seq(pdata, '&', EXPR_AND, psr_expr_primary);
}

static struct expr *psr_expr_xor(const char **pdata)
{
	return psr_seq(pdata, '^', EXPR_XOR, psr_expr_and);
}

static struct expr *psr_expr_or(const char **pdata)
{
	return psr_seq(pdata, '|', EXPR_OR, psr_expr_xor);
}

static struct expr *psr_expr_any(const char **pdata)
{
	return psr_expr_or(pdata);
}

static struct binding *psr_binding(const char **pdata)
{
	const char *data = *pdata;
	struct binding *b;
	struct expr *e;
	const char *p;

	e = psr_expr_any(&data);
	if (e == NULL)
		return NULL;

	if (strncmp(data, "<=", 2)) {
		expr_free(e);
		return NULL;
	}
	data += 2;
	psr_whitespace(&data);
	p = data;

	while (*p && *p != '\n')
		++p;

	b = calloc(1, sizeof(*b) + (p - data) + 1);
	if (b == NULL) {
		expr_free(e);
		return NULL;
	}

	memcpy(b->command, data, p - data);
	b->command[p - data] = 0;
	b->expr = e;

	*pdata = p + !!*p;

	psr_whitespace(pdata);

	return b;
}

struct binding *psr_parse(const char *data)
{
	struct binding *head = NULL;
	struct binding *next;
	struct binding *b;

	psr_whitespace(&data);

	while (data[0]) {
		b = psr_binding(&data);
		if (b == NULL)
			goto err;

		b->next = head;
		head = b;
	}

	return head;

err:
	for (b = head; b; b = next) {
		next = b->next;
		expr_free(b->expr);
		free(b);
	}
	return NULL;
}

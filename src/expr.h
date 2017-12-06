#ifndef __EXPR_H_
#define __EXPR_H_

#include "types.h"

enum expr_type {
	EXPR_OR,
	EXPR_XOR,
	EXPR_AND,
	EXPR_NOT,
	EXPR_DUR,
	EXPR_PRIMARY,
	EXPR_CINFO,
};

enum expr_cmp {
	EXPR_EQ,
	EXPR_NE,
	EXPR_LT,
	EXPR_GT,
	EXPR_LE,
	EXPR_GE,
};

struct expr_match {
	unsigned int lookup;
	enum expr_cmp cmp;
	int value;
};

struct expr {
	enum expr_type type;
	union {
		struct {
			struct expr *left;
			struct expr *right;
		} binop, or, and, xor, seq;

		struct expr *not;

		struct {
			unsigned int duration;
			struct expr *expr;
			u64 end;
		} dur;

		struct expr_match primary;
		struct expr_match cinfo;
	};
};

struct expr *expr_new(void);
void expr_free(struct expr *e);
int expr_cmp(struct expr_match *m, int value);

static inline unsigned int expr_typecode(unsigned int type, unsigned int code)
{
	return (type << 16) | code;
}

#endif

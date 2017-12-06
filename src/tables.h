#ifndef __TABLES_H_
#define __TABLES_H_

struct code_entry {
	const char *name;
	unsigned short type;
	unsigned short code;
};

extern const struct code_entry codetab[];
extern const unsigned int codetab_sz;

struct nametab_entry {
	const char *name;
	const char **tab;
};

extern const struct nametab_entry nametab[];
extern const unsigned int nametab_sz;

#endif

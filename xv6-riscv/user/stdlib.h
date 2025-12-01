// user/stdlib.h
#ifndef stdlib
#define stdlib

#include "kernel/types.h"

void *calloc(uint n, uint size);
void qsort(void *base, uint n_elem, uint size, int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, uint n_elem, uint size, int (*compare)(const void *, const void *));
int atoi(const char *s);
double atof(const char *s);

#endif

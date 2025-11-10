// user/stdlib.h
// Custom Standard Library Header for XV6
// Includes memory allocation, string conversion, and sorting/search functions

#ifndef STDLIB_H
#define STDLIB_H

#include "kernel/types.h" // For uint, etc.

// --- Memory Allocation ---
// calloc: Allocate and zero memory, robust against integer overflow
void* calloc(uint n, uint size);

// --- ASCII to Number Conversion ---
// int atoi(const char *s);      // Convert ASCII string to integer
double atof(const char *s);   // Convert ASCII string to floating-point double

// --- Sorting and Searching ---
void qsort(void *base, uint n_elem, uint size, int (*compare)(const void *, const void *));
void* bsearch(const void *key, const void *base, uint n_elem, uint size, int (*compare)(const void *, const void *));

#endif // STDLIB_H
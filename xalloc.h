#ifndef H_XALLOC
#define H_XALLOC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xmalloc (size_t);
void *xcalloc (size_t, size_t);

#define XFREE(p) (free (p), p = NULL)

#endif

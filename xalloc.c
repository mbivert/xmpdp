#include "xalloc.h"

void *
xmalloc (size_t n) {
   void *p = NULL;
   p = malloc (n);
   if (p == NULL) {
      fprintf (stderr, "Error: Out of memory.\n");
      exit (EXIT_FAILURE);
   }
   return p;
}

void *
xcalloc (size_t n_elem, size_t n) {
   void *p = NULL;
   p = xmalloc (n * n_elem);
   memset (p, 0, n * n_elem);
   return p;
}

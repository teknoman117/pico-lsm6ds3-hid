#ifndef DEBUG_H
#define DEBUG_H

// printf over RTT in Debug builds; compiled out otherwise.
#ifndef NDEBUG
#include <stdio.h>
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif

#endif

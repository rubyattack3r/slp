#ifndef SLP_STDLIB_H
#define SLP_STDLIB_H

#ifdef SLP_H
typedef struct SlpVM SlpVM;
#else
#include "slp_vm.h"
#endif

void slp_stdlib_init(SlpVM *vm);

#endif

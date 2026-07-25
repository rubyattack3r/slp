#ifndef SLP_EMBED_INTERNAL_H
#define SLP_EMBED_INTERNAL_H

#include "slp_vm.h"

void slp_embed_mark_roots(SlpVM *vm);
void slp_embed_release_all(SlpVM *vm);

#endif /* SLP_EMBED_INTERNAL_H */

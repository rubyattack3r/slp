#ifndef SLP_GC_H
#define SLP_GC_H

#include "slp_value.h"

void slp_gc_init(SlpVM *vm);
void slp_gc_free(SlpVM *vm);
void slp_gc_collect(SlpVM *vm);
void slp_gc_mark_obj(SlpVM *vm, SlpObj *obj);
void slp_gc_mark_value(SlpVM *vm, SlpValue value);
void slp_gc_sweep(SlpVM *vm);
SlpObj *slp_gc_allocate_object(SlpVM *vm, size_t size, SlpObjType type);
size_t slp_gc_object_size(SlpObj *obj);

#endif // SLP_H

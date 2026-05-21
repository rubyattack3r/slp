#ifndef SLEEPY_GC_H
#define SLEEPY_GC_H

#include "sleepy_value.h"

void sleepy_gc_init(SleepyVM *vm);
void sleepy_gc_free(SleepyVM *vm);
void sleepy_gc_collect(SleepyVM *vm);
void sleepy_gc_mark_obj(SleepyVM *vm, SleepyObj *obj);
void sleepy_gc_mark_value(SleepyVM *vm, SleepyValue value);
void sleepy_gc_sweep(SleepyVM *vm);
SleepyObj *sleepy_gc_allocate_object(SleepyVM *vm, size_t size, SleepyObjType type);
size_t sleepy_gc_object_size(SleepyObj *obj);

#endif // SLEEPY_H

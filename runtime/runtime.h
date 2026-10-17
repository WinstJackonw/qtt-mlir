#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *qtt_malloc(size_t size);
void qtt_free(void *pointer);

#ifdef __cplusplus
}
#endif

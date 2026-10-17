#include "runtime.h"
#include <stdlib.h>

void *qtt_malloc(size_t size) { return malloc(size); }
void qtt_free(void *pointer) { free(pointer); }

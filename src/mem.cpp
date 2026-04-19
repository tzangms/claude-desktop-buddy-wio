#include "mem.h"

#include <malloc.h>

uint32_t freeHeapBytes() {
  // sbrk(0) vs stack-pointer is unreliable on Seeed's SAMD51 linker layout.
  struct mallinfo mi = mallinfo();
  return static_cast<uint32_t>(mi.fordblks);
}

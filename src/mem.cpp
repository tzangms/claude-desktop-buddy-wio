#include "mem.h"

#include <malloc.h>

uint32_t freeHeapBytes() {
  // newlib's mallinfo() reports heap arena stats.
  // fordblks = total free bytes within sbrk-allocated arena.
  // The sbrk(0) / stack-pointer approach is unreliable on the
  // Seeed SAMD51 linker layout, so we use mallinfo() instead.
  struct mallinfo mi = mallinfo();
  return static_cast<uint32_t>(mi.fordblks);
}

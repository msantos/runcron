#include <stdint.h>
#include <stdlib.h>

static const uint32_t fnv1a32_prime = 16777619;
static const uint32_t fnv1a32_offset = 2166136261U;

uint32_t fnv1a(uint8_t *buf, size_t buf_size) {
  uint32_t h = fnv1a32_offset;
  for (size_t i = 0; i < buf_size; i++) {
    h = (h ^ buf[i]) * fnv1a32_prime;
  }
  return h;
}

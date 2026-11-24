#define QUILL_FUZZ
#include "../differential.c"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint64_t h = 14695981039346656037ULL;
    size_t i;
    if (size < 1)
        return 0;
    for (i = 0; i < size; i++)
        h = (h ^ data[i]) * 1099511628211ULL;
    rng = h | 1;
    check(data + 1, size - 1, delims[data[0] % 5], data[0] >> 7);
    return 0;
}

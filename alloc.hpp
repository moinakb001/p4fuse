#include "types.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>

struct arena_t
{
    u8 *data;
    u64 head;
    u64 tail;
    u64 total_size;
    s32 fd;
};

arena_t arena_init(s32 fd, u64 total_reserve, u64 total_size);
void *arena_alloc(arena_t *arena, u64 sz, u64 align);
void arena_clear(arena_t *arena);
void arena_free(arena_t *arena, u64 sz);
void arena_undo_alloc(arena_t *arena, u64 sz);
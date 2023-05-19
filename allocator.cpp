#include "alloc.hpp"

arena_t
arena_init
(
    arena_t *arena,
    s32 fd,
    u64 total_reserve,
    u64 total_size
)
{
    arena_t ar{};
    ar.fd = fd;
    ftruncate(fd, 0);
    ftruncate(fd, total_size);
    ar.data = (u8*)mmap(0, total_reserve, PROT_NONE, MAP_ANONYMOUS | MAP_SHARED | MAP_NORESERVE, -1, 0);
    mmap(ar.data, total_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    ar.total_size = total_size;
    ar.head = 0;
    ar.tail = 0;
    return ar;
}

void *
arena_alloc
(
    arena_t *arena,
    u64 sz,
    u64 align
)
{
    u64 news = (arena->tail + align - 1llu) &~ (align - 1llu);
    u64 stage2 = news + sz;
    u64 mask = arena->total_size - 1llu;
    if(((~mask) & stage2) != ((~mask) & arena->tail))
    {
        arena->tail = news = ((arena->tail + mask) &~ mask);
        stage2 = news + sz;
    }
    while ((stage2 - arena->head) >= arena->total_size)
    {
        u64 d = arena->tail - arena->head;
        arena->head &= mask;
        arena->tail = arena->head + d;
        arena->total_size <<= 1;
        ftruncate(arena->fd, arena->total_size);
        mmap(arena->data, arena->total_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, arena->fd, 0);
        mask = arena->total_size - 1llu;
        news = (arena->tail + align - 1llu) &~ (align - 1llu);
        stage2 = news + sz;
    }
    arena->tail = stage2;
    return &arena->data[news];
}

void
arena_clear
(
    arena_t *arena
)
{
    arena->head = 0;
    arena->tail = 0;
}
void
arena_free
(
    arena_t *arena,
    u64 sz
)
{
    arena->head += sz;
}
void
arena_undo_alloc
(
    arena_t *arena,
    u64 sz
)
{
    arena->tail -= sz;
}
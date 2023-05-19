#include "exec.hpp"
#include "types.hpp"

int queue_read(io_uring *pRing, arena_t *arena)
{
    u64 tail = arena->tail & (arena->total_size - 1llu);
    u64 head = arena->head & (arena->total_size - 1llu);
    auto osqe = io_uring_get_sqe(&ring);
    iovec iov []= {{&arena->data[tail], (arena->total_size) - tail},{arena->data, head)}};
    if (head == tail)
    {
        if(arena->head != arena->tail) return -1;
        arena->tail = arena->head = tail = head = 0;

    }

    if(head == 0) head = arena->total_size;

    if(tail > head)
        io_uring_prep_readv(osqe, 0,iov ,2,0);
    else
        io_uring_prep_read(osqe, 0, &arena->data[tail], head - tail);
    osqe->flags |= IOSQE_FIXED_FILE ;
    io_uring_sqe_set_data(osqe, (void*)msgtype_e::read_cmd);
    io_uring_submit(&ring);
    return 0;
}

void
exec_main
(
    io_uring *pRing,
    char *mountPath,
    char *strStore,
    char *inodeStore
)
{
    arena_t readArena;
    arena_t writeArena;
    u64 num_rqs = 0;
    u64 arena_full = 0;
    u64 mntLen = strlen(mountPath);
    while(1)
    {
        io_uring_cqe *pCqe;
        u64 index;
        while(io_uring_wait_cqes(&pRing, &pCqe, 1, 0, 0) != 0);
        io_uring_for_each_cqe(&pRing, index, pCqe)
        {
            switch((msgtype_e)pCqe->user_data)
            {
                case msgtype_e::init_open:
                {
                    char str[64];
                    aor(cqe->res >= 0);
                    aor(io_uring_register_files(&ring, &cqe->res, 1));
                    snprintf(str, 4096, "fd=%d,rootmode=40000,user_id=0,group_id=0,allow_other", cqe->res);
                    aor(mount("p4vfs", mountPath, "fuse.p4vfs", MS_NODEV, str));
                    aor(queue_read(pRing, &readArena));
                    break;
                }
                case msgtype_e::write_cmd:
                {
                    num_rqs--;
                    if(!num_rqs) writeArena.head = writeArena.tail = arena_full = 0;
                    break;
                }
                case msgtype_e::read_cmd:
                {
                    aor(pCqe->res);
                    readArena.tail += pCqe->res;
                    if (arena_full) continue;
                    aor(queue_read(pRing, &readArena));
                    if (process_commands(pRing, &readArena, &writeArena) == -2) arena_full = 1;
                    break;
                }
                case msgtype_e::sync_finished:
                {
                    ThreadContext *tc = &contexts[(u32)cqe->res];
                    auto sqe1 = io_uring_get_sqe(&pRing);
                    auto sqe2 = io_uring_get_sqe(&pRing);
                    u8 *v = arena_alloc(&writeArena, mntLen + tc->len + sizeof(fuse_out_header) + sizeof(fuse_open_out));
                    fuse_out_header *hdr = (fuse_out_header*) &v[mntLen + tc->len];
                    fuse_open_out   *out = (fuse_open_out*) &v[mntLen + tc->len + sizeof(fuse_out_header)];

                    __builtin_memcpy(v, mountPath, mntLen);
                    __builtin_memcpy(&v[mntLen], &tc->filename[1], tc->len - 1llu);
                    v[mntLen + tc->len - 1llu] = 0;
                    hdr->err = 0;
                    hdr->unique = tc->unique;
                    hdr->len = sizeof(fuse_out_header) + sizeof(fuse_open_out);
                    out->fh = tc->inode;
                    out->flags = FOPEN_KEEP_CACHE | FOPEN_PARALLEL_DIRECT_WRITES;
                    out->padding = 0;

                    io_uring_prep_openat(sqe1, AT_FDCWD, v, O_NOFOLLOW | O_RDONLY);
                    sqe1->flags |= IOSQE_IO_LINK | IOSQE_CQE_SKIP_SUCCESS;
                    io_uring_sqe_set_data(sqe1, (void*)msgtype_e::open_failed);
                    io_uring_prep_write(sqe2, 0, hdr, hdr->len);
                    sqe2->flags |= IOSQE_FIXED_FILE;
                    io_uring_sqe_set_data(sqe2, (void*)msgtype_e::write_cmd);
                    io_uring_submit(&pRing);
                    break;
                }
                case msgtype_e::enum_progress:
                {

                }
                case msgtype_e:: enum:finished:
                {
                    
                }
            }
        }
    }
}
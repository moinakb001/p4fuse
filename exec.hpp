#include "alloc.hpp"
#include "types.hpp"

enum class msgtype_e : u64
{
    read_cmd = 1,
    write_cmd = 2,
    sync_finished = 3,
    enum_finished = 4,
    enum_progress = 5,
    init_open = 6,
    open_failed = 7,
};

void loop_main(io_uring *ring);
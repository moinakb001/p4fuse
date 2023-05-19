#include <liburing.h>
#include <cstring>
#include <linux/futex.h>      /* Definition of FUTEX_* constants */
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>
#include "types.hpp"
#include "clientapi.h"
#include "p4libs.h"
#include "signaler.h"
#include "ftype.hpp"

enum class action_e: u32
{
    waiting = 0u,
    finished = 1u;
    list = 2u,
    sync = 3u,
};

struct ThreadContext;
class AsyncClient : public ClientUser {
    public:
    ThreadContext *tc;
    u8 bDir;
    virtual void OutputInfo(char level, const char *data) override;
    virtual void OutputStat( StrDict * dict) override;
};
struct NodeRes
{
    u64 size : 60;
    u64 bDir : 1;
    u64 type : 3;
    u8 len;
    u8 name[255];
};
#define NUM_RESULTS (1u << 8)
struct ThreadContext
{
    u8 filename[1<<15];
    u64 len;
    u64 unique;
    action_e action; 
    u32 waiting;
    ClientApi p4c;
    AsyncClient p4u;
    pthread_t thr;
    io_uring myring;
    io_uring *parent;
    NodeRes result[NUM_RESULTS];
    u32 num_res;
    u32 inode;
};
extern ThreadContext *contexts;
void *thread_main(void* param);
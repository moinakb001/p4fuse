#include "tp.hpp"

void
queue_thr_message
(
    ThreadContext *tc,
    u64 value
)
{
    auto sqe = io_uring_get_sqe(&tc->myring);
    io_uring_prep_msg_ring(sqe, tc->parent->ring_fd, tc-contexts, value, 0);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    io_uring_submit(&tc->myring);
}
#define MAX_RETRIES 100llu
u32 tp_wait_for_nval(u32 *loc, u32 *wait, u32 val)
{
    while(1)
    {
        for(u64 i = 0; i < MAX_RETRIES; i++)
        {
            u32 v = __atomic_load_n(loc, __ATOMIC_ACQUIRE);
            if (v != val)
            {
                return v;
            }
            __builtin_ia32_pause();
        }
        __atomic_store_n(wait, 1, __ATOMIC_RELEASE);
        syscall(SYS_futex, (loc, FUTEX_WAIT, 0, 0, 0, 0));
        __atomic_store_n(wait, 0, __ATOMIC_RELEASE);
    }
}

u64
inc_result_num
(
    ThreadContext *tc
)
{
    u64 old = __atomic_fetch_add(&tc->num_res, 1, __ATOMIC_ACQ_REL);
    if (old == NUM_RESULTS)
    {
        queue_thr_message(tc, msgtype_e::emum_progress);
        while(__atomic_load_n(&tc->num_res, __ATOMIC_ACQUIRE)==(NUM_RESULTS+1llu))
            syscall(SYS_futex, (u32*)&tc->num_res, FUTEX_WAIT, NUM_RESULTS+1llu, 0, 0, 0);
        old = __atomic_fetch_add(&tc->num_res, 1, __ATOMIC_ACQ_REL);
    }
    return old;
}

void
AsyncClient::OutputStat
(
    StrDict *dict
)
{
    u64 idx = inc_result_num(this->tc);
    auto nr = &this->tc->result[idx];
    char *filenm = &(dict->GetVar( "depotFile" )->Text()[this->tc->len]);
    char *types = dict->GetVar( "headType" )->Text();
    auto szstr = dict->GetVar( "fileSize" );
    if(!szstr) return;

    nr->len = strlen(filenm);
    __builtin_memcpy(nr->name, filenm, nr->len);
    nr->size = szstr->Atoi();
    nr->type = (u64)((strncmp(types, "symlink", sizeof("symlink")-1) == 0) ?
            ftype_e::symlink : ftype_e::regfile);
}

void 
AsyncClient::OutputInfo
(
    char level,
    const char *data
)
{
    if (!(level == '1' && this->bDir))
    {
        return;
    }
    u64 idx = inc_result_num(this->tc);
    u64 offs =  4llu;
    u8 *filename = (u8*)&data[offs];
    auto nr = &this->tc->result[idx];
    nr->len = (u8)strlen((char*)filename);
    __builtin_memcpy(nr->name, filename, nr->len);
    nr->type = (u64)ftype_e::directory;
}

void *thread_main(void* param)
{
    u32 idx = (u32)((u64)param);
    ThreadContext *tc=&contexts[idx];
    Error e;
    StrBuf msg;

    // TODO: do we need p4Initialize per-thread.

    tc->p4c.SetProtocol( "tag" );
    tc->p4c.Init(&e);
    if( e.Test() )
    {
        e.Fmt( &msg );
        fprintf( stderr, "%s\n", msg.Text() );
        return 0;
    }

    action_e action = (action_e)tp_wait_for_nval(&tp->action, &tp->waiting, (u32)action_e::waiting);

    while(1)
    {
        
        switch(action)
        {
            case action_e::sync:
            {
                char *argv[] = {(s8*)"-f", (char*)tc->filename};
                tc->filename[tc->len - 1llu] = 0;
                tc->num_res = 0;
                tc->p4c.SetArgv(2, argv);
                tc->p4c.Run("sync", &tc->p4u);
                tc->action = action_e::finished;
                queue_thr_message(tc, msgtype_e::sync_finished);
            }
            case action_e::list:
            {
                u8 *path = tc->filename;
                char *filecmd[] = {(s8*)"-Ol",(s8*)"-T",(s8*)"depotFile,fileSize,headType", (char*)path};
                tc->filename[tc->len] = '*';
                tc->filename[tc->len+1] = 0;
                tc->p4u.bDir = 1;
                tc->p4c.SetArgv(1, (char**)&path);
                tc->p4c.Run("dirs", &tc->p4u);
                tc->p4u.bDir = 0;
                tc->p4c.SetArgv(4, filecmd);
                tc->p4c.Run("fstat", &tc->p4u);
                tc->action = action_e::finished;
                queue_thr_message(tc, msgtype_e::emum_finished);
            }
        }
        action = (action_e) tp_wait_for_nval(&tp->action, &tp->waiting, (u32)action_e::finished);
        if(action == action::waiting)
            action = (action_e)tp_wait_for_nval(&tp->action, &tp->waiting, (u32)action_e::waiting);
    }
}


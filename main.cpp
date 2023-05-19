#include <cstdio>
#include <liburing.h>
#include "types.hpp"
#include <pthread.h>
#include <sys/syscall.h>       /* Definition of SYS_* constants */
#include <unistd.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include "fuse_kernel.h"

# include "clientapi.h"
# include "p4libs.h"
# include "signaler.h"
//#undef printf
//#define printf(...) (__VA_ARGS__)
#include "inode_store.cpp"


// ?????? openssl why are you like this
extern "C"
{
    int FIPS_mode_set(int x)
    {
        return 0;
    }
}

io_uring ring{};

pthread_mutex_t mut{};

u8 *read_buf;
u64 cur=0, head=0;
#define BUF_SZ (1llu << 25)

u8 *out_buf;
u64 cur_sz;
u64 num_rqs;

u8 * alloc_chunk(u64 sz, u64 align)
{
    u64 save = cur_sz;
    cur_sz = (cur_sz + align - 1llu) & ~(align - 1llu);
    u8 *res = &out_buf[cur_sz];
    cur_sz += sz;
    if(cur_sz > BUF_SZ)
    {
        cur_sz = save;
        return NULL;
    }
    return res;
}

int queue_read()
{
    u64 th = head &((BUF_SZ)-1llu);
    iovec iov []= {{&read_buf[th], (BUF_SZ) - th},{read_buf, cur}};
    auto osqe = io_uring_get_sqe(&ring);
    io_uring_prep_readv(osqe, 0,iov ,2,0);
    osqe->flags |= IOSQE_FIXED_FILE ;
    io_uring_sqe_set_data(osqe, (void*)1);
    io_uring_submit(&ring);
    return 0;
}

pthread_t thr[128];
io_uring  trings[128];
#define min(a,b) (((a) < (b)) ? (a) : (b))

void copyin_wrap(u8 *dst, u64 sz, u64 begin)
{
    begin = begin &((BUF_SZ) - 1llu);
    u64 first_cp = min(sz, BUF_SZ-begin);
    __builtin_memcpy(dst, &read_buf[begin], first_cp);
    if(first_cp != sz)
    {
        __builtin_memcpy(&dst[first_cp], read_buf, sz-first_cp);
    }
}
template <typename T>
void copyin_wrap(T *str, u64 begin)
{
    copyin_wrap((u8*)str, sizeof(T), begin);
}
#define NUM_NODES_SHIFT 8
#define NUM_NODES (1llu << NUM_NODES_SHIFT)

char paths[NUM_NODES][8192];
u64  dir_bmp[NUM_NODES - 6];
u64 pidx = 0;



extern "C" {
struct EVP_PKEY;
struct X509;
struct SSL;
int EVP_PKEY_get_base_id(const EVP_PKEY *pkey);
X509 *SSL_get1_peer_certificate(const SSL *ssl);

int EVP_PKEY_base_id(const EVP_PKEY *pkey)
{
    return EVP_PKEY_get_base_id(pkey);
}

X509 *SSL_get_peer_certificate(const SSL *ssl)
{
    return SSL_get1_peer_certificate(ssl);
}
};
enum class ctype_e : u64
{
    dirs = 0,
    files = 1,
    file = 2,
    change = 3,
    changes = 4,
    sync = 5
};
inode_table_t *table;

void gc_node(inode_entry_t *, u8 *){

}
class MyClientUser : public ClientUser
{
    public:
    ctype_e type;
    s64 err;
    u64 len;
    u32 id;
    u32 node;
    u8 name[4096];
    u64 nameid;
    
    MyClientUser() : ClientUser()
    {

    }
    
    virtual void OutputInfo( char level, const char *data) override
    {
        printf("%c %d\n", level, type);
        if (this->err < 0) return;

        if ((this->type == ctype_e::dirs && level == '1') ||
            (this->type == ctype_e::files && level == '1' && strncmp(data, "depotFile ", sizeof("depotFile ")-1) == 0))
        {
            
            char *d = (char*)&data[(this->type == ctype_e::dirs ? 4llu : sizeof("depotFile ")-1llu)+this->len];
            u64 len = strlen(d);
            __builtin_memcpy(this->name, d, len);
            printf("%s\n", d);
            this->nameid = len;
            this->node = 0;
            if(this->type == ctype_e::dirs)
            {
                u32 node = find_or_insert_path<gc_node>(table, id, this->name, this->nameid);
                if(node != table->id) return;
                auto ent = &table->entries[node];
                ent->bList = 0;
                ent->parent = id;
                ent->bDir = 1;
                ent->bSym = 0;
                ent->left = 0;
                ent->right = 0;
                ent->slen = this->nameid;
                __builtin_memcpy(node_name(table, ent), this->name, this->nameid);
                this->node = node;
                print_node(table, ent);
            }
        }
        if(this->type == ctype_e::files && level == '1' && strncmp(data, "fileSize ", sizeof("fileSize ")-1) == 0)
        {
            printf("%s\n", data);
            if(this->node == 0)
            {
                u32 node = find_or_insert_path<gc_node>(table, id, this->name, this->nameid);
                if(node != table->id) return;
                auto ent = &table->entries[node];
                ent->bList = 0;
                ent->parent = id;
                ent->bDir = 0;
                ent->left = 0;
                ent->bSym = 0;
                ent->slen = this->nameid;
                ent->right = 0;
                __builtin_memcpy(node_name(table, ent), this->name, this->nameid);
                this->node = node;
            }
            sscanf(data, "fileSize %llu", &table->entries[this->node].size);
            
        }
        if(this->type == ctype_e::files && level == '1' && strncmp(data, "headType ", sizeof("headType ")-1) == 0)
        {
            printf("%s\n", data);
            char *d=(char*)&data[sizeof("headType ")-1];
            if(this->node == 0)
            {
                u32 node = find_or_insert_path<gc_node>(table, id, this->name, this->nameid);
                if(node != table->id) return;
                auto ent = &table->entries[node];
                ent->bList = 0;
                ent->parent = id;
                ent->bDir = 0;
                ent->left = 0;
                ent->right = 0;
                ent->bSym = 0;
                ent->slen = this->nameid;
                __builtin_memcpy(node_name(table, ent), this->name, this->nameid);
                this->node = node;
            }
            table->entries[this->node].bSym = strncmp(d, "symlink", sizeof("symlink")-1) == 0;
            printf("type %s %d\n", d, node);
        }

        
    };
    virtual void OutputText(const char *test, int len) override
    {
        if(len == 0) return;
        u8 *palloc = alloc_chunk(len, 1);
       // printf("%d, %llx\n", len, palloc);
        __builtin_memcpy(palloc, test, len);
    }
    virtual void OutputBinary(const char *test, int len) override
    {
        if(len == 0) return;
        u8 *palloc = alloc_chunk(len, 1);
        //printf("%d, %llx\n", len, palloc);
        __builtin_memcpy(palloc, test, len);
    }
};
ClientApi p4c;
MyClientUser p4u;
void initializeP4API()
{
    StrBuf msg;
    Error e;
    P4Libraries::Initialize(P4LIBRARIES_INIT_ALL, &e);
    signal(SIGINT, SIG_DFL); // unset the default set by global signaler in C++ so it does not exit 
    signaler.Disable(); // disable the global signaler memory tracking at runtime

    if( e.Test() )
    { 
        e.Fmt( &msg );
        fprintf( stderr, "%s\n", msg.Text() );
        return;
    }

    // Any special protocol mods

    p4c.SetProtocol( "tag" );
    
    // Enable client-side Extensions
   // client.EnableExtensions(&e);

    // Connect to server
    
    p4c.Init( &e );
    
    if( e.Test() )
    {
        e.Fmt( &msg );
        fprintf( stderr, "%s\n", msg.Text() );
        return;
    }

}

void send_message(fuse_out_header *outh)
{
    auto osqe = io_uring_get_sqe(&ring);
    io_uring_prep_write(osqe, 0, outh, outh->len, 0);
    osqe->flags |= IOSQE_FIXED_FILE ;
    io_uring_sqe_set_data(osqe, (void*)2);
    io_uring_submit(&ring);
}
void send_message_offs(fuse_out_header *outh, u64 offs)
{
    u8 *as = (u8*)&outh[1];
    auto osqe = io_uring_get_sqe(&ring);
    iovec iovecs[] = {{outh, sizeof(fuse_out_header)},{&as[offs],outh->len - sizeof(fuse_out_header)}};
    io_uring_prep_writev(osqe, 0, iovecs, 2, 0);
    osqe->flags |= IOSQE_FIXED_FILE ;
    io_uring_sqe_set_data(osqe, (void*)2);
    io_uring_submit(&ring);
}

#define FUSE_MESSAGE_PROLOGUE(inkind, name, outkind) \
    inkind name; \
    copyin_wrap(&name, cur+sizeof(hdr)); \
    u8 * sad = alloc_chunk(sizeof(fuse_out_header) + sizeof(outkind), 1); \
    fuse_out_header *outh = (fuse_out_header*)sad; \
    outkind   *iout = (outkind*) &sad[sizeof(fuse_out_header)]; \
    if(!sad) return -2; \
    outh->len = sizeof(fuse_out_header) + sizeof(outkind); \
    outh->error = 0; \
    outh->unique = hdr.unique;

fuse_attr get_attr(char *path)
{

}

u64 is_dir(u64 inode)
{
    inode -= 2;
    inode &= NUM_NODES - 1llu;

    return (dir_bmp[inode >> 6] >> (inode & 63llu) ) & 1llu;
    
}

void alloc_dirs(u32 node, u64 prev){
        if(!strncmp((char*)node_name(table, &table->entries[table->entries[node].parent]), "unix", table->entries[table->entries[node].parent].slen))
            printf("HIAD\n");
                    auto ent = &table->entries[node];
                    fuse_dirent *nn = (fuse_dirent*)alloc_chunk(sizeof(fuse_dirent) + ent->slen, 8);
                    __builtin_memcpy(nn->name,node_name(table, ent), ent->slen);
                    nn->type = ent->bDir ? (S_IFDIR >> 12) : (S_IFREG >> 12);
                    nn->off = (cur_sz - prev + 7llu) &~ 7llu;
                    nn->ino = ent - table->entries;
                    nn->namelen = ent->slen;
                }
void alloc_dirsplus(u32 node, u64 prev){
                    auto ent = &table->entries[node];
                    fuse_direntplus *nn = (fuse_direntplus*)alloc_chunk(sizeof(fuse_direntplus) + ent->slen, 8);
                    __builtin_memcpy(nn->dirent.name,node_name(table, ent), ent->slen);
                    nn->dirent.type = ent->bDir ? (S_IFDIR >> 12) : (S_IFREG >> 12);
                    nn->dirent.off = (cur_sz - prev + 7llu) &~ 7llu;
                    nn->dirent.ino = ent - table->entries;
                    nn->dirent.namelen = ent->slen;
                    nn->entry_out.nodeid = node;
                    nn->entry_out.generation = ent->gen;
                    nn->entry_out.attr.ino = node;
                    nn->entry_out.attr.nlink = 2;
                    nn->entry_out.attr.mode = 0444;
                    nn->entry_out.attr.size = ent->bDir ? 0 : ent->size;
                    nn->entry_out.attr.mode |= ent->bDir ? S_IFDIR : (S_IFREG | 0111);
               //     nn->entry_out.attr_valid = 1llu << 60;
                 //   nn->entry_out.attr_valid_nsec = 1<< 30;

                }

int process_commands()
{
    while(1)
    {
loop_head:
        if (cur + sizeof(fuse_in_header) > head)
        {
            return -1;
        }
        fuse_in_header hdr;

        copyin_wrap(&hdr, cur);
        if(cur + hdr.len > head)
        {
            return -1;
        }
        printf("opc %d inode %d\n", hdr.opcode, hdr.nodeid);
        switch(hdr.opcode)
        {
            case FUSE_INIT:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_init_in, init_in, fuse_init_out);

                u64 flagsin = init_in.flags;
                if(flagsin & FUSE_INIT_EXT)
                    flagsin |= ((u64)init_in.flags2)<<32;

                
                
                iout->major = 7;
                iout->minor = 28;
                iout->max_readahead = init_in.max_readahead;
                u64 flagsout = 
                    FUSE_ASYNC_READ |
                    FUSE_PARALLEL_DIROPS |
                    FUSE_EXPLICIT_INVAL_DATA |
                    FUSE_HANDLE_KILLPRIV |
                    FUSE_ASYNC_DIO |
                    FUSE_IOCTL_DIR |
                    FUSE_ATOMIC_O_TRUNC |
                    FUSE_SPLICE_READ |
                    FUSE_POSIX_LOCKS |
                    FUSE_FLOCK_LOCKS |
                    FUSE_MAX_PAGES |
                    FUSE_BIG_WRITES |
                    FUSE_NO_OPENDIR_SUPPORT |
                    FUSE_NO_OPEN_SUPPORT |
                    FUSE_CACHE_SYMLINKS /*|
                    FUSE_DO_READDIRPLUS |
                    FUSE_READDIRPLUS_AUTO*/;
                    

                flagsout &= flagsin;
                iout->flags = (u32) flagsout;
                if(flagsin & FUSE_INIT_EXT)
                    iout->flags2 = (u32)(flagsout >> 32);

                iout->max_pages = 1<<15;
                iout->max_write = BUF_SZ >> 3;
                iout->time_gran = 1;
                iout->max_background = (1<<16)-1;
                iout->congestion_threshold = 1<<15;
                num_rqs++;
                send_message(outh);
                cur += hdr.len;
                break;
                //syscall(SYS_exit_group, -1); 
            }
            case FUSE_GETATTR:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_getattr_in, attr_in, fuse_attr_out);
                fuse_attr fattr;
                u64 bDir = 1;

                if(attr_in.getattr_flags & FUSE_GETATTR_FH)
                {
                    hdr.nodeid = attr_in.fh;
                }
                auto ent = &table->entries[hdr.nodeid];
                //iout->attr_valid = 1llu << 60;
                //iout->attr_valid_nsec = 1<< 30;
                *iout = fuse_attr_out{};
                iout->dummy = 0;

                iout->attr = fuse_attr{};
                iout->attr.ino = hdr.nodeid;
                iout->attr.nlink = 2;
                iout->attr.mode = 0444;
                iout->attr.size = ent->bDir ? 0 : ent->size;
                iout->attr.mode |= ent->bDir ? S_IFDIR : (S_IFREG | 0111);
                if(ent->bSym) iout->attr.mode = S_IFLNK;
                print_node(table, ent);

                outh->len = sizeof(fuse_out_header) + sizeof(fuse_attr_out);
                outh->error = 0;
                outh->unique = hdr.unique;

getattr_out:
                num_rqs++;
                send_message(outh);
                cur += hdr.len;
                break;
            }
            case FUSE_GETXATTR:
            case FUSE_LISTXATTR:
            case FUSE_IOCTL:
            case FUSE_OPENDIR:
            {
                fuse_out_header *outh = (fuse_out_header*)alloc_chunk(sizeof(fuse_out_header), 1);
                if(outh == 0) return -2;
                outh->len = sizeof(fuse_out_header);
                outh->error = -ENOSYS;
                outh->unique = hdr.unique;
                cur += hdr.len;
                num_rqs++;
                send_message(outh);
                break;
            }
            case FUSE_LSEEK:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_lseek_in, seek_in, fuse_lseek_out);
                iout->offset = lseek(table->entries[seek_in.fh].opened_file, seek_in.offset, seek_in.whence);
                outh->len = sizeof(fuse_out_header) + sizeof(fuse_lseek_out);
                outh->error = 0;
                outh->unique = hdr.unique;
                cur += hdr.len;
                num_rqs++;
                send_message(outh);
                break;
            }
            case FUSE_FLUSH:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_flush_in, flush_in, u8);
                s32 i = fsync(table->entries[flush_in.fh].opened_file);
                outh->len = sizeof(fuse_out_header);
                outh->error = i < 0 ? -errno : 0;
                outh->unique = hdr.unique;
                cur += hdr.len;
                num_rqs++;
                send_message(outh);
                break;
            }
            case FUSE_ACCESS:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_access_in, access_in, u8);
                
                outh->len = sizeof(fuse_out_header);
                outh->error = 0;
                outh->unique = hdr.unique;
                cur += hdr.len;
                num_rqs++;
                send_message(outh);
                break;
            }
            case FUSE_STATFS:
            {
                fuse_out_header *outh = (fuse_out_header*)alloc_chunk(sizeof(fuse_out_header), 1);
                fuse_statfs_out *arg = (fuse_statfs_out*)alloc_chunk(sizeof(fuse_statfs_out), 1);;
                if(outh == 0) return -2;

                *arg = fuse_statfs_out{};

                outh->len = sizeof(fuse_out_header) + sizeof(fuse_statfs_out);
                outh->error = 0;
                outh->unique = hdr.unique;
                cur += hdr.len;
                num_rqs++;
                send_message(outh);
                break;
            }
            case FUSE_READLINK:
            {
                fuse_out_header *outh = (fuse_out_header*)alloc_chunk(sizeof(fuse_out_header), 1);
                auto psz = path_size(table, hdr.nodeid);
                char *path = (char*)__builtin_alloca(psz);
                build_path(table, hdr.nodeid, (u8*)&path[psz]);
                char *pathsec[] = {"-f", path};
                path[psz-1] = 0;
                if(!table->entries[hdr.nodeid].bList){
                    printf("path %s\n", path);
                    char *pathsec[] = {"-f", path};
                    path[psz-1] = 0;
                    table->entries[hdr.nodeid].bList = 1;
                    path[psz-1] = 0;
                    p4u.type = ctype_e::sync;
                    p4c.SetArgv(2, pathsec);
                    p4c.Run("sync", &p4u);
                }
                path[0] = 'a';
                u64 msz = 128;
                u8 *buf = alloc_chunk(128, 1);
                if(!buf) return -2;
                s64 mad  = 0;
                        //printf("HERE %llx\n", __LINE__);
                while((mad = readlink((char*)path,(char*)buf, msz )) == msz)
                {
                            //printf("HERE %llx\n", __LINE__);
                    if(!alloc_chunk(msz, 1)) return -2;
                    msz <<=1;
                }
                buf[mad] = 0;
                printf("link %s\n", buf);
                outh->len = sizeof(fuse_out_header) + (mad < 0 ? 0 : mad);
                outh->error = mad < 0 ? mad : 0;
                outh->unique = hdr.unique;
                cur += hdr.len;
                num_rqs++;
                send_message(outh);
                break;

            }
            case FUSE_OPEN:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_open_in, open_in, fuse_open_out);
                /*fuse_notify_inval_entry_out *outarg = (fuse_notify_inval_entry_out*)alloc_chunk(sizeof(fuse_notify_inval_entry_out), 1);
                u8 *st = (u8*)alloc_chunk(table->entries[hdr.nodeid].slen+1, 1);
                if(outarg == 0 || st == 0) return -2;*/
                auto psz = path_size(table, hdr.nodeid);
                char *path = (char*)__builtin_alloca(psz);
                build_path(table, hdr.nodeid, (u8*)&path[psz]);
                char *pathsec[] = {"-f", path};
                path[psz-1] = 0;
                if(!table->entries[hdr.nodeid].bList){
                    printf("path %s\n", path);
                    char *pathsec[] = {"-f", path};
                    path[psz-1] = 0;
                    table->entries[hdr.nodeid].bList = 1;
                    path[psz-1] = 0;
                    p4u.type=ctype_e::sync;
                    p4c.SetArgv(2, pathsec);
                    p4c.Run("sync", &p4u);
                }
                if(table->entries[hdr.nodeid].opened_file == 0 && !table->entries[hdr.nodeid].bSym)
                {
                    path[0] = 'a';
                    s32 files = open(path,O_RDWR | O_NOFOLLOW);
                    table->entries[hdr.nodeid].opened_file = files;
                    
                }
                table->entries[hdr.nodeid].refcnt++;
                /*{
                    
                    *outarg = fuse_notify_inval_entry_out{};
                    outarg->parent = table->entries[hdr.nodeid].parent;
                    outarg->namelen = table->entries[hdr.nodeid].slen;
                    __builtin_memcpy(st, node_name(table, &table->entries[hdr.nodeid]), table->entries[hdr.nodeid].slen);
                    st[table->entries[hdr.nodeid].slen] = 0;
                    outh->len = sizeof(fuse_out_header) + sizeof(fuse_notify_inval_entry_out) + table->entries[hdr.nodeid].slen+1;
                    outh->error = FUSE_NOTIFY_INVAL_ENTRY;
                    outh->unique = 0;
                    send_message(outh);
                }*/
                *iout = fuse_open_out{};
                iout->fh = hdr.nodeid;
                iout->open_flags = FOPEN_NONSEEKABLE;
                outh->len = sizeof(fuse_out_header) + sizeof(fuse_open_out);
                outh->error = 0;
                outh->unique = hdr.unique;
                cur += hdr.len;
                num_rqs++;
                send_message(outh);
                break;
            };

            case FUSE_RELEASE:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_release_in, release_in, u8);
                auto ent = &table->entries[release_in.fh];
                ent->refcnt--;
                if(!ent->refcnt && ent->opened_file != 0)
                {
                    close(ent->opened_file);
                    ent->opened_file = 0;
                }
                outh->len = sizeof(fuse_out_header);
                outh->error = 0;
                outh->unique = hdr.unique;
                cur+=hdr.len;
                num_rqs++;
                send_message(outh);
                break;

            }
            case FUSE_READDIR:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_read_in, read_in, fuse_dirent);
                cur_sz -= sizeof(fuse_dirent);
                auto psz = path_size(table, hdr.nodeid);
                char *path = (char*)__builtin_alloca(psz + 128);
                build_path(table, hdr.nodeid, (u8*)&path[psz]);
                char *pathsec[] = {"-e", path};
                u64 offs = 0;

                outh->len = sizeof(fuse_out_header);
                outh->error = 0;
                outh->unique = hdr.unique;

                path[psz] = '*';
                path[psz+1] = 0;
                p4u.len = psz;
                p4u.type = ctype_e::dirs;
                p4u.err = 0;
                p4u.id = hdr.nodeid;
                if(!table->entries[hdr.nodeid].bList){
                    char *pathsec[] = {"-Ol","-T","depotFile,fileSize,headType", path};
                p4c.SetArgv(1, &path);

                p4c.Run("dirs", &p4u);
                p4u.type = ctype_e::files;
                p4c.SetArgv(4, pathsec);
                p4c.Run("fstat", &p4u);
                u64 add = 0;
                table->entries[hdr.nodeid].bList = 1;
                }
               
                auto t = cur_sz;

                find_all_with_parent<alloc_dirs>(table, hdr.nodeid, 1,  t);
                auto ss = cur_sz - t;
                ss = (ss + 7llu) &~ 7llu;
                outh->len = sizeof(fuse_out_header);
                offs = read_in.offset;
                outh->len += min(ss - read_in.offset, read_in.size);
readdir_out:
                num_rqs++;
                send_message_offs(outh, offs);
                cur += hdr.len;
                break;
            }
            /*case FUSE_READDIRPLUS:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_read_in, read_in, fuse_dirent);
                cur_sz -= sizeof(fuse_dirent);
                auto psz = path_size(table, hdr.nodeid);
                char *path = (char*)__builtin_alloca(psz + 128);
                build_path(table, hdr.nodeid, (u8*)&path[psz]);
                char *pathsec[] = {"-Ol","-T","depotFile,fileSize,headType", path};
                u64 offs = 0;

                outh->len = sizeof(fuse_out_header);
                outh->error = 0;
                outh->unique = hdr.unique;

                path[psz] = '*';
                path[psz+1] = 0;
                p4u.len = psz;
                p4u.type = ctype_e::dirs;
                p4u.err = 0;
                p4u.id = hdr.nodeid;

                p4c.SetArgv(1, &path);
                p4c.Run("dirs", &p4u);
                p4u.type = ctype_e::files;
                p4c.SetArgv(4, pathsec);
                p4c.Run("fstat", &p4u);
                u64 add = 0;
               
                auto t = cur_sz;

                find_all_with_parent<alloc_dirsplus>(table, hdr.nodeid, 1, t);
                auto ss = cur_sz - t;
                ss = (ss + 7llu) &~ 7llu;
                outh->len = sizeof(fuse_out_header);
                offs = read_in.offset;
                outh->len += min(ss - read_in.offset, read_in.size);
                offs = read_in.offset;
                num_rqs++;
                send_message_offs(outh, offs);
                cur += hdr.len;
                break;
            }*/
            case FUSE_LOOKUP:
            {
                FUSE_MESSAGE_PROLOGUE(u8, stin, fuse_entry_out);
                u8 *buf = (u8*)__builtin_alloca(hdr.len - sizeof(fuse_in_header));
                copyin_wrap(buf, hdr.len - sizeof(fuse_in_header), cur + sizeof(hdr));
                outh->len = sizeof(fuse_out_header) + sizeof(fuse_entry_out);
                outh->error = 0;
                outh->unique = hdr.unique;
                u64 n = find_path(table, hdr.nodeid, buf, hdr.len - sizeof(fuse_in_header) - 1llu);
                //print_node(table, temp);
                if(n >> 32 != 0 && table->entries[hdr.nodeid].bDir && !table->entries[hdr.nodeid].bList)
                {
                    auto psz = path_size(table, hdr.nodeid);
                    char *path = (char*)__builtin_alloca(psz + 128);
                    build_path(table, hdr.nodeid, (u8*)&path[psz]);
                    char *pathsec[] = {"-Ol","-T","depotFile,fileSize,headType", path};
                    path[psz] = '*';
                    path[psz+1] = 0;
                    p4u.len = psz;
                    p4u.type = ctype_e::dirs;
                    p4u.err = 0;
                    p4u.id = hdr.nodeid;

                    p4c.SetArgv(1, &path);
                    p4c.Run("dirs", &p4u);
                    p4u.type = ctype_e::files;
                    p4c.SetArgv(4, pathsec);
                    p4c.Run("fstat", &p4u);
                    table->entries[hdr.nodeid].bList = 1;
                }
                n = find_path(table, hdr.nodeid, buf, hdr.len - sizeof(fuse_in_header) - 1llu);
                auto temp = &table->entries[n &((1llu<<32)-1llu)];
                print_node(table, temp);
                if(n>> 32 != 0 ) {
                    //print_node(table, temp);
                    outh->len = sizeof(fuse_out_header);
                    outh->error = -ENOENT;
                    num_rqs++;
                    send_message(outh);
                    cur += hdr.len;
                    break;
                }
                print_node(table, temp);
             //   iout->attr_valid = 1llu << 60;
               // iout->attr_valid_nsec = 1<< 30;
               *iout = fuse_entry_out{};
                iout->nodeid = n;
                
                iout->generation = table->entries[n].gen;

                iout->attr = fuse_attr{};
                iout->attr.ino = n;
                iout->attr.nlink = 2;
                iout->attr.mode = 0444;
                iout->attr.size = table->entries[n].bDir ? 0 : table->entries[n].size;
                if(table->entries[n].bDir) iout->attr.mode |= S_IFDIR;
                else if(!table->entries[n].bSym) iout->attr.mode |=  (S_IFREG|0111);
                else iout->attr.mode =  S_IFLNK;
                //iout->attr.mode |= (table->entries[n].bSym  && !table->entries[n].bDir)? S_IFLNK: (S_IFREG | 0111) ;

                num_rqs++;
                send_message(outh);
                cur += hdr.len;
                break;
            }
            case FUSE_READ:
            {
                FUSE_MESSAGE_PROLOGUE(fuse_read_in, read_in, u8);
                cur_sz--;
                auto t = cur_sz;
                
                
                u8 *buf = alloc_chunk(read_in.size, 1);
                if(!buf) return -2;
                auto fd = table->entries[read_in.fh].opened_file;
#if 0
                auto ss = cur_sz - t;
                
                ss = ss < read_in.offset ? 0 : ss - read_in.offset;
#else
                auto ss = pread(fd, buf, read_in.size, read_in.offset);
#endif
                outh-> len =  min(ss, read_in.size) + sizeof(fuse_out_header);
                outh->unique = hdr.unique;
                outh->error = 0;
                num_rqs++;
                send_message(outh);
                cur += hdr.len;

                break;
            }
            case FUSE_INTERRUPT:
            case FUSE_FORGET:
            case FUSE_BATCH_FORGET:
            {
                cur+=hdr.len;
                break;
            }
            default:
            {
                printf("ERROR unhandled opcode %d\n", hdr.opcode);
                syscall(SYS_exit_group, -1); 
            }
        }

    }
}

void *thr_main(void *a)
{
    u64 thrid = (u64)a;

    return 0;
}

int main(int argc, char **argv)
{
    inode_table_t tb;
    table = &tb;

    init_table(table, open("strf.temp", O_CREAT | O_RDWR), open("inf.temp", O_CREAT | O_RDWR));
    initializeP4API();
    io_uring_cqe *cqe;

    if (argc == 1) return -1;

    for (u64 i = 0; i< 128; i++)
    {
        io_uring_params params{};
        if (i != 0)
        {
            params.flags = IORING_SETUP_ATTACH_WQ;
            params.wq_fd = trings[0].ring_fd;
        }
        io_uring_queue_init_params(256, &trings[i], &params);
        pthread_create(&thr[i], 0, thr_main, (void*)i);

    }
    

    aor(io_uring_queue_init(256, &ring, 0));

    auto osqe = io_uring_get_sqe(&ring);

    io_uring_prep_openat(osqe, -1, "/dev/fuse", O_RDWR, 0);
    io_uring_sqe_set_data(osqe, (void*)30);
    read_buf =(u8*) mmap(0, BUF_SZ, PROT_READ|PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    out_buf =(u8*) mmap(0, BUF_SZ, PROT_READ|PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    iovec iov = {read_buf, BUF_SZ};
    io_uring_register_buffers(&ring, &iov,1);

    aor(io_uring_submit_and_wait(&ring, 1));
    auto queue_next = 0;

    while(1)
    {
        u64 headiness;
        int ret;
        while(io_uring_wait_cqes(&ring, &cqe, 1, 0, 0) != 0);
        if (!cqe)
        {
            continue;
        }
        io_uring_for_each_cqe(&ring, headiness, cqe)
        {
            switch((u64)cqe->user_data)
            {
                case 30:
                {
                    char str[4096];
                    aor(io_uring_register_files(&ring, &cqe->res, 1));
                    snprintf(str, 4096, "fd=%d,rootmode=40000,user_id=0,group_id=0,allow_other", cqe->res);
                    aor(mount("p4vfs", argv[1], "fuse.p4vfs", MS_NODEV, str));
                    aor(queue_read());
                    /*snprintf(str, 4096, "lowerdir=./a:./test");
                    aor(mount("overlay", "mnter", "overlay", MS_NODEV, str));*/
                    
                    
                    
                    break;
                }
                case 1:
                {
                    aor(cqe->res);
                    head += cqe->res;
                    if(!queue_next)
                    {
                        aor(queue_read());
                        if (process_commands() == -2)
                        {
                            queue_next = 1;
                        }
                    }
                    break;
                }
                case 2:
                {
                    num_rqs--;
                    if (num_rqs == 0)
                    {
                        cur_sz = 0;
                        queue_next = 0;
                    }
                }
            }
            io_uring_cq_advance(&ring, 1);
        }
    }

    io_uring_queue_exit(&ring);

    return 0;
}
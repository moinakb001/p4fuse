#include <linux/futex.h>      /* Definition of FUTEX_* constants */
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

struct inode_entry_t
{
    union 
    {
        u64 name_offset;
        u8 name[8];
    };
    u32 parent;
    u32 slen: 8; // max filename size of 256, if <= 8, s
    u32 bDir: 1;
    u32 bSym: 1;
    u32 bBlack: 1; //string rbtree red/black
    u32 bList: 1;
    u32 gen: 16;
    u64 size;
    u32 left;
    u32 right;
    s32 opened_file;
    u32 refcnt;
};

 struct arena_t
{
    u8 *data;
    u64 head;
    u64 cur;
    u64 allocsz;
};

struct inode_table_t
{
    u32 id;
    u32 total_inodes;
    u32 gen;
    s32 strfd;
    s32 inodefd;
    arena_t str_arena;
    inode_entry_t *entries;
};

u8 *node_name(inode_table_t *table, inode_entry_t *entry)
{
    return entry->slen <= 8 ? entry->name : &table->str_arena.data[entry->name_offset];
}
void print_str(u8 *s, u64 len)
{
    for(u64 i = 0;i<len;i++)
    {
        putchar(s[i]);
    }
}

s32 cmp_node(inode_table_t *table, inode_entry_t *entry,  u32 parent, u8 *str, u8 len)
{
    u8 *str1 = node_name(table, entry);
    u8 tlen = entry->slen;
    tlen = tlen < len ? tlen : len;
    if(parent != entry->parent) return parent - entry->parent;
    for(u64 i = 0;i<tlen;i++)
    {
        if(str1[i] < str[i]) return 1;
        if(str1[i] > str[i]) return -1;
    }
    return ((s32)len) - ((s32)entry->slen);
}

u64 find_path(inode_table_t *table, u32 parent, u8 *str, u8 len)
{
    u32 cur = 0;
    s32 cc;
    inode_entry_t *ent = &table->entries[1];
    while((cc = cmp_node(table, ent, parent, str, len)) != 0)
    {
        cur = cc < 0 ? ent->left : ent->right;
        if(cur == 0)
            return (1llu << 32) | (ent - table->entries);

        ent = &table->entries[cur];
    }
    return cur;
}

u8 *arena_alloc_chunk(arena_t *arena, u64 sz, u64 align)
{
    u64 old_sz = arena->cur;
    u64 cur = (arena->cur + align - 1llu) &~ (align - 1llu);
    u64 after = cur + sz;
    if (after - __atomic_load_n(&arena->head, __ATOMIC_ACQUIRE) > arena->allocsz)
    {
        return NULL;
    }
    arena->cur = after;
    return &arena->data[cur& (arena->allocsz - 1llu)];
}

u8 *arena_alloc_chunk_atomic_cushion(arena_t *arena, u64 sz, u64 align, u64 cushion)
{
    u64 expected = __atomic_load_n(&arena->cur, __ATOMIC_ACQUIRE);
    u64 desired;
    u64 ret;
    do {
        ret = (expected + align - 1llu) &~ (align - 1llu);
        desired = ret + sz;
        if( (desired+cushion) -__atomic_load_n(&arena->head, __ATOMIC_ACQUIRE)>= arena->allocsz)
            return NULL;
    } while(!__atomic_compare_exchange_n(&arena->cur, &expected, desired, 1, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
    return &arena->data[ret & (arena->allocsz - 1llu)];
}

u8 *arena_alloc_chunk_atomic(arena_t *arena, u64 sz, u64 align)
{
    return arena_alloc_chunk_atomic_cushion(arena, sz, align, 0);
}

void arena_clear(arena_t *arena)
{
    __atomic_store_n(&arena->cur, 0, __ATOMIC_RELEASE);
}
#if 0
u64 rwlock_read_acq(s32 *lock, s32 max)
{
    s32 val = __atomic_add_fetch(write, 1, __ATOMIC_ACQ_REL);
    if(val > 0)
    {
        return 1;
    }
    val = __atomic_sub_fetch(lock, 1, __ATOMIC_ACQ_REL);
    return 0;
}
void atomic_read_rel(s32 *lock, s32 max)
{
    s32 v = __atomic_sub_fetch(lock, 1, __ATOMIC_ACQ_REL);
}
#define MAX_LOCK_TRIES 100
void rwlock_write_acq(s32 *write,u32 *pending, s32 max)
{
    u32 v = __atomic_exchange_n(pending, 1, __ATOMIC_ACQ_REL);
    s32 val;
    for(u64 i = 0; i < MAX_LOCK_TRIES; i++)
    {
        if(v == 0)
            goto next;
        v = __atomic_exchange_n(pending, 1, __ATOMIC_ACQ_REL);
        __builtin_ia32_pause();
    }
next:
    val = __atomic_sub_fetch(write, max, __ATOMIC_ACQ_REL);
    
    for(u64 i = 0; i < MAX_LOCK_TRIES; i++)
    {
        if (val == -max)
        {
            return;
        }
        val = __atomic_load_n(write, __ATOMIC_ACQUIRE);
        __builtin_ia32_pause();
    }
    syscall(SYS_futex, write, FUTEX_WAIT, val, 0, 0, 0);
}
#endif
#define RESERVED_INODES 2u
u32 next_id(inode_table_t *tab)
{
    u32 lid;
    do {
        lid = __atomic_add_fetch(&tab->id, 1, __ATOMIC_ACQ_REL);
    } while(lid < RESERVED_INODES);
    
    if(lid == RESERVED_INODES)
    {
        __atomic_fetch_add(&tab->gen, 1, __ATOMIC_ACQ_REL);
    }
    return lid;
}

template<auto free_fn>
u32 new_inode(inode_table_t *table, u8 name_len)
{
    u32 id = next_id(table);
    u32 gen = __atomic_load_n(&table->gen, __ATOMIC_ACQUIRE);
    
    if(id >= table->total_inodes && table->total_inodes != 0)
    {
        u64 sz = table->total_inodes * sizeof(inode_entry_t);
        table->total_inodes <<= 1;
        printf("err@%d %d\n", __LINE__, errno);
        printf("%d\n", fsync(table->inodefd));
        printf("err@%d %d\n", __LINE__, errno);
        ftruncate(table->inodefd, sz << 1);
        printf("err@%d %d\n", __LINE__, errno);
        mmap(&table->entries[table->total_inodes >> 1], sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, table->inodefd, sz);
        printf("err@%d %d\n", __LINE__, errno);
        fsync(table->inodefd);
    }
    auto ent = &table->entries[id];
    if(gen != 0 && ent->slen > 8)
    {
        free_fn(ent, node_name(table, ent));
        __atomic_fetch_add(&table->str_arena.head, ent->slen, __ATOMIC_RELEASE);
    }
    if (name_len > 8)
    {
        u32 flags;
        u8 * offs = arena_alloc_chunk_atomic(&table->str_arena, name_len, 1);
        while(offs == 0)
        {
            u64 al = table->str_arena.allocsz;
            table->str_arena.allocsz <<= 1;
            printf("%d\n", fsync(table->strfd));
            printf("err@%d %d\n", __LINE__, errno);
            printf("%d\n",ftruncate(table->strfd, al << 1));
            printf("err@%d %d\n", __LINE__, errno);
            printf("%d\n",mmap( &table->str_arena.data[0], al<<1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, table->strfd, 0));
            printf("err@%d %d\n", __LINE__, errno);
            printf("%d\n",mmap( &table->str_arena.data[al<<1], al<<1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, table->strfd, 0));
            printf("err@%d %d\n", __LINE__, errno);
            fsync(table->strfd);
            printf("err@%d %d\n", __LINE__, errno);
            offs = arena_alloc_chunk_atomic(&table->str_arena, name_len, 1);
        }
        ent->name_offset = offs - table->str_arena.data;
    }
    ent->gen = gen;
    ent->slen = name_len;
    return id;
}

void init_table(inode_table_t *table, s32 strfd, s32 inodefd, u32 id = 1, u32 gen = 0, u32 inodes = 1 << 10,u64 arenasz = 1llu << 16)
{
    table->id = id;
    table->gen = gen;
    table->total_inodes = inodes;
    table->str_arena.allocsz = arenasz;
    table->str_arena.head = 0;
    table->strfd = strfd;
    table->inodefd = inodefd;
    table->entries = (inode_entry_t*)mmap(0, (1llu << 32) * sizeof(inode_entry_t), PROT_NONE, MAP_ANONYMOUS | MAP_SHARED | MAP_NORESERVE, -1, 0);
    printf("err@%d %d\n", __LINE__, errno);
    table->str_arena.data = (u8*) mmap(0, (1llu << 40), PROT_NONE, MAP_ANONYMOUS | MAP_SHARED | MAP_NORESERVE, -1, 0);
    printf("err@%d %d\n", __LINE__, errno);
    ftruncate(table->strfd, table->str_arena.allocsz);
    printf("err@%d %d\n", __LINE__, errno);
    mmap( &table->str_arena.data[0], table->str_arena.allocsz , PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, table->strfd, 0);
    printf("err@%d %d\n", __LINE__, errno);
    mmap( &table->str_arena.data[table->str_arena.allocsz], table->str_arena.allocsz , PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, table->strfd, 0);
    printf("err@%d %d\n", __LINE__, errno);
    ftruncate(table->inodefd, sizeof(inode_entry_t) * table->total_inodes);
    printf("err@%d %d\n", __LINE__, errno);
    printf("res %llx\n", mmap(&table->entries[0], sizeof(inode_entry_t) * table->total_inodes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, table->inodefd, 0));
    printf("err@%d %d\n", __LINE__, errno);
    table->str_arena.cur = 0;
    table->entries[1].parent = 0;
    table->entries[1].name[0]='/';
    table->entries[1].slen = 1;
    table->entries[1].bDir = 1;
    table->entries[1].gen = 0;
    table->entries[1].left = 0;
    table->entries[1].right = 0;
}
void print_node(inode_table_t *table, inode_entry_t *inode)
{
    u8 * nn = node_name(table, inode);
    printf("node ");
    print_str(nn, inode->slen);
    printf(" - id %lld, left %d, right %d, bSym %d, parent %d\n", inode - table->entries, inode->left, inode->right, inode->bSym, inode->parent);


}

template<auto fn, typename... T>
void find_all_with_parent(inode_table_t *table, u32 parent, u32 node, T... var)
{
    inode_entry_t *ent = &table->entries[node];
    print_node(table, ent);
    if(ent->parent == parent)
    {
        
        fn(node, var...);
        if(ent->left != 0) find_all_with_parent<fn>(table, parent, ent->left, var...);
        if(ent->right != 0) find_all_with_parent<fn>(table, parent, ent->right, var...);
        return;
    }

    else if(ent->parent < parent){
     if(ent->right != 0) find_all_with_parent<fn>(table, parent, ent->right, var...);
    }
    else if(ent->parent > parent && ent->left != 0) find_all_with_parent<fn>(table, parent, ent->left, var...);
}

template<auto free_fn>
u32 find_or_insert_path(inode_table_t *table, u32 parent, u8 *str, u8 len)
{
    u32 cur = 1;
    s32 cc;
    inode_entry_t *ent = &table->entries[1];
    while((cc = cmp_node(table, ent, parent, str, len)) != 0)
    {
        cur = cc < 0 ? ent->left : ent->right;

        if(cur == 0)
        {
            u32 id = new_inode<free_fn>(table, len);
            print_node(table, ent);
            if(cc < 0) ent->left = id;
            else ent->right = id;
            return id;
        }

        ent = &table->entries[cur];
    }
    return cur;
}
u64 path_size(inode_table_t *table, u32 node)
{
    auto ent = &table->entries[node];
    u64 path = 0;
    while(ent != table->entries)
    {
        path += 1+ent->slen;
        ent = &table->entries[ent->parent];
    }
    return path;
}
void build_path(inode_table_t *table, u32 node, u8 *end)
{
    auto ent = &table->entries[node];
    while(ent != table->entries)
    {
        end--;
        *end='/';
        end -= ent->slen;
        __builtin_memcpy(end, node_name(table, ent), ent->slen);
        ent = &table->entries[ent->parent];
    }
    return;
}
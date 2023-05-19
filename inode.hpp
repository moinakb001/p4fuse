#include "types.hpp"
#include "alloc.hpp"

enum class node_state_e: u32
{
    uninit = 0u,
    dispatched = 1u,
    hydrated = 2u,
};

struct inode_entry_t
{
    union 
    {
        u64 name_offset;
        u8 name[8];
    };
    u32 parent;
    u32 slen: 8; // max filename size of 256, if <= 8, store in name
    u32 fType: 3;
    u32 bBlack: 1; //string rbtree red/black
    u32 state: 2;
    u32 gen: 16;
    u64 size;
    u32 left;
    u32 right;
    s32 opened_file;
    u32 refcnt;
};

struct inode_table_t
{
    u32 id;
    u32 total_inodes;
    u32 gen;
    arena_t str_arena;
    inode_entry_t *entries;
};

static inline u8 *inode_name(inode_table_t *table, inode_entry_t *entry)
{
    return entry->slen <= 8 ? entry->name : &table->str_arena.data[entry->name_offset];
}

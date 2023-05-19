#include "inode.hpp"

s64 inode_str_cmp(inode_table_t *table, inode_entry_t *entry, u8 *str, u64 len, u32 parent)
{
    u8 *name = node_name(table, entry);
    u64 nlen = len < entry->slen ? len : entry->slen;
    if (entry->parent != parent)
    {
        return parent - entry->parent;
    }
    for (u64 i = 0; i < nlen; i++)
    {
        if(str[i] != name[i]) return str[i] - name[i];
    }
    return len - entry->slen;
}

u64 find_node(inode_table_t *table, u8 *str, u8 len, u32 parent)
{
    u32 cur = 0;
    s32 cc;
    inode_entry_t *ent = &table->entries[1];
    while((cc = cmp_node(table, ent, str, len, parent)) != 0)
    {
        cur = cc < 0 ? ent->left : ent->right;
        if(cur == 0)
            return (1llu << 32) | (ent - table->entries);

        ent = &table->entries[cur];
    }
    return cur;
}
void do_rotation(inode_table_t *table, u32 *par_slot, u32 dir)
{
    auto par = *par_slot;
    auto pare = &table->entries[par];
    auto ch_slot = dir ? &pare->left : &pare->right;
    auto ch = *ch_slot;
    auto che = &table->entries[ch];
    auto gc_slot = dir ? &che->right : &che->left;
    auto gc = *gc_slot;
    *par_slot = ch;
    *gc_slot = par;
    *ch_slot = gc;
}

template<auto free_fn>
u32 find_or_insert_path(inode_table_t *table, u32 parent, u8 *str, u8 len)
{
    s32 cc;
    inode_entry_t *ent = &table->entries[1];
    u32 stack[32];
    u32 bitmask = 0
    u8 sptr = *get_root(table);
    stack[0] = 1;
    while((cc = cmp_node(table, ent, parent, str, len)) != 0)
    {
        cur = cc < 0 ? ent->left : ent->right;
        bitmask <<= 1;
        bitmask |= !(cc < 0) ;
        stack[sptr] = cur;
        sptr++;
        if(cur == 0)
        {
            break;
        }
        ent = &table->entries[cur];
    }
    if (cur != 0)
        return cur;
    u32 id = new_inode<free_fn>(table, len);
    stack[sptr] = id;
    while (sptr >= 2)
    {
        auto parent = &table->entries[stack[sptr - 1]];
        auto gp = &table->entries[stack[sptr - 2]];
        auto unc = &table->entries [((bitmask >> 1)&1) ? gp->left : gp->right];
        auto ggp = get_root(table);
        if(sptr > 2)
        {
            auto ent = &table->entries[stack[sptr-3]];
            ggp = ((bitmask >> 2) & 1) ? &ent->right : &ent->left;

        }
        
        if(parent->bBlack)
        {
            break;
        }
        if (!unc->bBlack)
        {
            parent->bBlack = 1;
            unc->bBlack = 1;
            gp->bBlack = 0;
            sptr -= 2;
            bitmask >>= 2;
            continue;
        }
        if((bitmask & 1) != ((bitmask >> 1) & 1))
        {
            do_rotation(table,((bitmask >> 1)&1) ? &gp->right : &gp->left, bitmask & 1);
            bitmask ^= 1;
            parent = &table->entries[stack[sptr]];
        }
        u32 temp = gp->bBlack;
        gp->bBlack = parent->bBlack;
        parent->bBlack = temp;
        do_rotation(table, ggp, bitmask & 1);
        bitmask >>= 2;
        sptr -= 2;
    }
    return id;
}
u32 insert_node(inode_table_t *table, u8 *str, u8 len, u32 parent)
{
    u32 cur = 0;
    s32 cc;
    inode_entry_t *ent = &table->entries[1];
    while((cc = cmp_node(table, ent, str, len, parent)) != 0)
    {
        cur = cc < 0 ? ent->left : ent->right;
        if(cur == 0)
        {
            insert_node_with_parent()
        }
        ent = &table->entries[cur];
    }
    return cur;
}


template<auto fn, typename... T>
void find_all_with_parent(inode_table_t *table, u32 parent, u32 node, T... var)
{
    inode_entry_t *ent = &table->entries[node];
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
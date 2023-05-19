using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
using u128 = unsigned __int128;

using s8 = char;
using s16 = short;
using s32 = int;
using s64 = long long;
using s128 = __int128;

using f32 = float;
using f64 = double;

#define aor(x) do {\
    auto ret____VVV = x; \
    if (ret____VVV < 0) \
    { \
        printf("ERROR in " #x ": file " __FILE__ ":%d, errno %d\n", __LINE__,ret____VVV); \
        syscall(SYS_exit_group, ret____VVV); \
    } \
    } while(0)
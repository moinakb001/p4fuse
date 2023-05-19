#include <unistd.h>
#include <fcntl.h>
#include "types.hpp"
#include <stdio.h>
int main()
{
    u8 buf[4096];
    u64 asd = readlinkat(open("/back/projs/p4vfs/test/sw/tools/unix/hosts/Linux-x86/unix-build/bin/", O_RDWR), "nvmake", (char*)buf, 4096);
    buf[asd] = 0;
    printf("%s\n", buf);
    return 0;
}
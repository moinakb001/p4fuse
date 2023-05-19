#!/bin/sh
cd /back/projs/p4vfs/
rm -rf aout.*
umount -f temp
mount -t tmpfs tmpfs temp
mkdir temp/use
mkdir temp/work
umount -f mnter
mount -t overlay overlay -o lowerdir=./a:./test,upperdir=./temp/use,workdir=./temp/work mnter/
#cp /back/projs/p4vfs/mnter/sw/tools/unix/hosts/Linux-x86/unix-build/bin/nvmake .
#ldd /back/projs/p4vfs/mnter/sw/tools/unix/hosts/Linux-x86/unix-build/bin/nvmake
cd mnter/sw/dev/gpu_drv/chips_a/drivers/resman/
/back/projs/p4vfs/mnter/sw/misc/linux/unix-build --tools /back/projs/p4vfs/mnter/sw/tools nvmake resman develop amd64 -j128
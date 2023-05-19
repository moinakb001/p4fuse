#!/bin/sh
umount test
umount test
umount test
rm *temp
rm -rf a/*
clang++ -DOS_LINUX -I ./p4/include/p4/ -luring ./main.cpp -L ./p4/lib/ -lclient -lp4api -lcrypto -lssl -lrt -lp4script_c -lp4script_curl -lp4script_sqlite -std=c++2a -g -O0
./a.out test

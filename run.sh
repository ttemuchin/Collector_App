#!/bin/bash
make -f makefile

./log &

sleep 1

./s1 &
./s2 &

./cli

# pkill -f log
# pkill -f s1
# pkill -f s2

# gcc KV1_cli.c -o cli \ 
#     `pkg-config --cflags gtk+-3.0` \
#     `pkg-config --libs gtk+-3.0` \
#     -lpthread

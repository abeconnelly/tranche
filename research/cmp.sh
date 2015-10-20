#!/bin/bash

gcc -Wall -g hello_fuse.c `pkg-config fuse --cflags --libs` -o hello_fuse
gcc -Wall hello.c `pkg-config fuse --cflags --libs` -o hello

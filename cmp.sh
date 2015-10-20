#!/bin/bash
#

gcc -Wall tranche.c `pkg-config fuse --cflags --libs` -o bin/tranche

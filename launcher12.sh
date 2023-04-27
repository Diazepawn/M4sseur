#!/bin/bash
T=`mktemp`
tail -c +139 "$0"|xz -d|g++-12 -xc++ -march=native -std=c++20 -pthread -O3 -DNDEBUG -o$T -
(sleep 3;rm $T)&exec $T

#!/bin/bash
T=`mktemp`
tail -c +136 "$0"|xz -d|c++ -xc++ -march=native -std=c++20 -pthread -O3 -DNDEBUG -o$T -
(sleep 3;rm $T)&exec $T

#!/bin/sh
c++ nanosrc/nanonizer.cpp -std=c++20 -march=native -O3 -DNDEBUG -o nanonizer_gcc

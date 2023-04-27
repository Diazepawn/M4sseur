#!/bin/sh
g++-12 src/main.cpp -march=native -std=c++20 -pthread -O3 -DNDEBUG -o M4sseur_gcc

#!/bin/sh
c++ src/main.cpp -xc++ -march=native -std=c++20 -pthread -O3 -DNDEBUG -o M4sseur_gcc

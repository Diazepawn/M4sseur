wsl g++-12 src/main.cpp -std=c++20 -march=native -pthread -O3 -fomit-frame-pointer -DNDEBUG -foptimize-sibling-calls -fno-exceptions -DUSE_SSE41 -DUSE_SSSE3 -DUSE_SSE2 -DUSE_VNNI -DUSE_AVXVNNI -DUSE_AVX2 -mavxvnni -msse4.1 -mssse3 -msse2 -mavx2 -mbmi -o M4sseur_gcc

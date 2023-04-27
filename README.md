# M4sseur

![M4sseur logo](logo.png?raw=true)

Stronk 4k chess engine written by Maik Guntermann (Germany) in C++20

- Heavy optimized plain vanilla Negamax (Alpha-Beta) search using lazy SMP and a new approach for move-ordering (improving Multitasking performance)
- Simple but effective evaluation function
- For TCEC build, the transposition table ("Hash Map") size is 64 GB and 52 threads are used
- Other builds uses 8 GB/8 threads by default

---

## 4k Build

Currently, the final TCEC version can be build on Linux by:
```
lbuild_tcec.sh
```
(creates the executable file `./M4sseur_tcec`)

## Other builds

A 'non-4k' version can be build either on Linux by:
```
lbuild_linux.sh
```

or on Windows by:
```
wbuild_mingw.bat
```
(requires MinGW)

---

The Linux binaries can also be build from Windows by using WSL:
```
build_linux.bat
```

Or for explicit GCC 12 build (requires g++-12):
```
build_linux12.bat
```

The project and solutions files for Windows Visual Studio 2019+ are present in the root folder.

## Settings

The number of threads, the HashTable size and other debugging/testing options for the non-4k builds can by set in `src/main.cpp`. The macro `TRACK_PV` enables output of PV/NPS/search depth etc. in both the non-4k build and other builds.

## UCI Support

For 4k build:

- `uci` `isready` `position moves [MOVES]` `go` `quit` 

Additional support for non-4k builds:

- `position fen [FEN] moves [MOVES]` `ucinewgame`

Note that `stop` is not supported.

## Requires

GCC 12 (best) or GCC 11 (slower) on Linux; Visual Studio 2019+ and/or MinGW on Windows.

## TODOs

Make a makefile

#!/bin/bash
./lbuild_linux_nanonizer.sh
./nanonizer_gcc
./compress.sh
cat launcher12.sh nanoout/phase2.cpp.lzma >M4sseur_tcec
chmod +x M4sseur_tcec
echo Final size=`du -b M4sseur_tcec`

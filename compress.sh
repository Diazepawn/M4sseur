#!/bin/bash

fle="nanoout/phase2.cpp"
fle_compr="nanoout/phase2.cpp.lzma"

compress()
{
  xz -k -f --format=lzma --lzma1=$opt $fle
}

compress_size()
{
  xz -k -c --format=lzma --lzma1=$opt $fle | wc -c
}
compress_info()
{
  xz -k --format=lzma --lzma1=$opt $fle --verbose --verbose
}

find_config_slow()
{
  for ((lc=0; lc<=3; lc+=1)); do
    for ((lp=0; lp<=1; lp+=1)); do
      for ((pb=0; pb<=2; pb+=1)); do
        for ((depth=0; depth<=256; depth+=256)); do
          for mf in hc3 hc4 bt2 bt3 bt4; do
            for nice in {4..273}; do
	          opt="preset=9e,lc=$lc,lp=$lp,pb=$pb,mf=$mf,mode=normal,nice=$nice,depth=$depth"
              size=$(compress_size)
              if (( size < size_best )); then
                echo "Best so far $opt Size $size"
                opt_best=$opt
                size_best=$size
              fi
            done
          done
        done
      done
    done
  done
}

find_config_fast()
{
  for ((lc=0; lc<=2; lc+=2)); do
    for mf in hc3 hc4 bt2 bt3 bt4; do
      for nice in {4..273}; do
        opt="preset=9e,lc=$lc,lp=0,pb=0,mf=$mf,mode=normal,nice=$nice,depth=0"
        size=$(compress_size)
        if (( size < size_best )); then
          echo "Best so far $opt Size $size"
          opt_best=$opt
          size_best=$size
        fi
      done
    done
  done
}

opt_best=""
size_best=10000000

#find_config_slow
find_config_fast

echo " ---- Best Options $opt_best Best Size $size_best"
opt=$opt_best

compress

compress_info

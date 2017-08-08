# GTS
A GPU-accelerated in-memory trajectory storage system, supporting two basic kinds of queries: range query and top-k similarity query.

## Compile ##
VS2015: open the project file

Linux: `make all -jN`

where N is the number of cores of your CPU

## Configuration ##
open multi-GPU mode: `#define USE_MULTIGPU` in ConstDefine.h

parameters: in ConstDefine.h

## Run ##
Just directly execute the compiled and linked executive file.

## Process ##
17/8/2: testing whether STIG works on multi-GPU environment

17/8/7: add multi-thread CPU version of range query

#!/usr/bin/env bash
# Copyright (c) 2012 Cloudera, Inc. All rights reserved.

TARGET_BUILD_TYPE=Debug

# parse command line options
for ARG in $*
do
  case "$ARG" in
    -codecoverage)
      TARGET_BUILD_TYPE=CODE_COVERAGE_DEBUG
      ;;
    -help)
      echo "make_debug.sh [-codecoverage]"
      echo "[-codecoverage] : build with 'gcov' code coverage instrumentation at the cost of performance"
      exit
      ;;
  esac
done

cd $IMPALA_HOME
bin/gen_build_version.py
cmake -DCMAKE_BUILD_TYPE=$TARGET_BUILD_TYPE .
make clean

rm -f $IMPALA_HOME/llvm-ir/impala-nosse.ll
rm -f $IMPALA_HOME/llvm-ir/impala-sse.ll

cd $IMPALA_HOME/common/function-registry
make
cd $IMPALA_HOME/common/thrift
make
cd $IMPALA_BE_DIR
make -j4

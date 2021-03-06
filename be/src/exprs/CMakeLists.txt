# Copyright (c) 2012 Cloudera, Inc. All rights reserved.

# where to put generated libraries
set(LIBRARY_OUTPUT_PATH "${BUILD_OUTPUT_ROOT_DIRECTORY}/exprs")

# where to put generated binaries
set(EXECUTABLE_OUTPUT_PATH "${BUILD_OUTPUT_ROOT_DIRECTORY}/exprs")

add_library(Exprs
  agg-expr.cc
  arithmetic-expr.cc
  binary-predicate.cc
  bool-literal.cc
  case-expr.cc
  cast-expr.cc
  compound-predicate.cc
  conditional-functions.cc
  date-literal.cc
  expr.cc
  float-literal.cc
  function-call.cc
  in-predicate.cc
  int-literal.cc
  is-null-predicate.cc
  like-predicate.cc
  literal-predicate.cc
  math-functions.cc
  null-literal.cc
  opcode-registry.cc
  slot-ref.cc
  string-literal.cc
  string-functions.cc
  timestamp-functions.cc
  timestamp-literal.cc
  timezone_db.cc
  utility-functions.cc
)

target_link_libraries(Exprs
  Util
  ThriftSaslTransport
  boost_regex-mt
)

#add_executable(expr-test expr-test.cc)
#target_link_libraries(expr-test ${IMPALA_TEST_LINK_LIBS})
#add_test(expr-test ${BUILD_OUTPUT_ROOT_DIRECTORY}/exprs/expr-test)

#!/bin/sh

# Expecting MESOS_SOURCE_DIR and MESOS_BUILD_DIR to be in environment.

env | grep MESOS_SOURCE_DIR >/dev/null

test $? != 0 && \
  echo "Failed to find MESOS_SOURCE_DIR in environment" && \
  exit 1

env | grep MESOS_BUILD_DIR >/dev/null

test $? != 0 && \
  echo "Failed to find MESOS_BUILD_DIR in environment" && \
  exit 1

# Check that the JavaException framework crashes and prints an
# ArrayIndexOutOfBoundsExcpetion. This is a test to be sure that Java
# exceptions are getting propagated. Th exit status of grep should be 0.

$MESOS_BUILD_DIR/src/examples/java/test-exception-framework local 2>&1 \
  | grep "ArrayIndexOutOfBoundsException"

exit $?

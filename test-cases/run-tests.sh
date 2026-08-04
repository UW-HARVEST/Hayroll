#!/bin/bash
set -x

./generate-absolute-paths.sh
for test_case in */; do
  # strip trailing /
  test_case="${test_case%/}"
  ./run-test.sh $test_case
done

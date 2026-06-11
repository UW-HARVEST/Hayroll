#!/bin/bash
test_case=$1

set -x
exec ../build/hayroll transpile ${test_case}/compile_commands.json --output-dir /tmp/hayroll-output/${test_case}

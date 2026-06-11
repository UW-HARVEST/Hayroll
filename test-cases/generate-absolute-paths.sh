#!/bin/bash
# expand $HAYROLL_ROOT and $TEST_CASE_DIR variables to generate compile_commands.json files from compile_commands.json.in templates
# required because compile_commands.json files must use absolute paths.

for i in **/compile_commands.json.in; do
  HAYROLL_ROOT="$(git rev-parse --show-toplevel)"
  TEST_CASE_DIR="$HAYROLL_ROOT/test-cases/${i%/compile_commands.json.in}"
  <$i sed -r -e 's!\$HAYROLL_ROOT!'"$HAYROLL_ROOT"'!g' -e 's!\$TEST_CASE_DIR!'"$TEST_CASE_DIR"'!g' > ${i%.in}
done

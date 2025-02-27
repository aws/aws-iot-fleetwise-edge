#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -xeuo pipefail

# We can't use solutions like pytest-cpp (https://github.com/pytest-dev/pytest-cpp) or
# gtest-parallel (https://github.com/google/gtest-parallel) for running tests with valgrind because
# the way those tools work is by listing all tests from a binary and then spawning a new process for
# each test. This normally work fine since the overhead to start a new C++ process is very low, but
# valgrind can add an overhead of several seconds during startup, making parallel tests actually
# slower.
# So for valgrind we follow a similar approach: list the tests, but then use GNU parallel to run
# multiple processes with each one getting a subset of the tests.
# Please note that some systems have a much simpler parallel command (coming from moreutils package
# on Ubuntu), which doesn't have the options we need (specially -X which group the arguments in
# batches of equal size).
if ! parallel --help |  grep '\-X'; then
    echo "The parallel command doesn't seem to be GNU parallel. Please install it to continue."
    exit 1
fi

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --binary)
            BINARY=$2
            shift
            ;;
        --valgrind-command)
            VALGRIND_COMMAND=$2
            shift
            ;;
        --help)
            echo "Usage: $0 --binary <BINARY_WITH_GOOGLE_TESTS> <OPTIONS>"
            echo "  --valgrind-command <ARGS>            The full valgrind command including any options, but excluding the binary to be executed."
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

if [ "${BINARY:?'Missing --binary argument'}" == "" ]; then
    exit 1
fi

if [ "${VALGRIND_COMMAND:?'Missing --valgrind-command argument'}" == "" ]; then
    exit 1
fi

# Since we are already using pytest-cpp plugin for the regular unit tests, we use it for listing
# the tests. From the output of --gtest_list_tests it isn't straightforward to build the full test
# name to use it in --gtest_filter. pytest-cpp already parses that and --collect-only displays
# the full test name:
# https://github.com/pytest-dev/pytest-cpp/blob/ea5c1de7ac99a0c346162903b1e07d74070068f9/src/pytest_cpp/google.py#L40
DISCOVERED_TESTS=$(pytest --collect-only ${BINARY} | grep CppItem | sed -E 's/.*<CppItem (.+?)>/\1/')

# We shuffle the list of tests to better distribute tests among workers. Slow tests tend to be in
# the same test suite, so a worker could be assigned a lot of those slow tests and end up being the
# bottleneck.
# For more details about how GNU parallel works: https://www.gnu.org/software/parallel/parallel.html#tutorial
ALL_JOBS_SUCCEEDED=1
mkdir -p logs
JOBS_LOG=logs/parallel-jobs-valgrind_${BINARY}.log
echo "${DISCOVERED_TESTS}" | shuf | parallel --verbose --joblog ${JOBS_LOG} --no-run-if-empty -X -j 100% "
  set -xeuo pipefail
  export PYTEST_XDIST_WORKER=gw\$((\${PARALLEL_SEQ} - 1))
  echo {} | tr ' ' ':' | xargs -I '[]' ${VALGRIND_COMMAND} ./${BINARY} --gtest_filter='[]' --gtest_output=xml:report-valgrind_${BINARY}-\${PARALLEL_SEQ}.xml
" || ALL_JOBS_SUCCEEDED=0

echo "Summary of all jobs:"
cat ${JOBS_LOG}
expr $ALL_JOBS_SUCCEEDED || (echo "Some jobs failed, check logs/ or the output above for more details" && exit 1)

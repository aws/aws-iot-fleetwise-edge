# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import os
import re
import subprocess
from typing import List, Optional

from tenacity import Retrying as TenacityRetrying
from tenacity import retry as tenacity_retry
from tenacity import retry_if_exception_type, stop_after_delay, wait_exponential

ENABLED_VALUES = ["true", "1", "on", "yes"]


def get_own_ip_address():
    cmd = "ip route get 1 | sed -n 's/^.*src \\([0-9.]*\\) .*$/\\1/p'"
    return subprocess.check_output(cmd, shell=True).decode("utf-8").strip()


def is_hil():
    return os.environ.get("TEST_HIL", "false").lower() in ENABLED_VALUES


def retry(*args, **kwargs):
    """Override tenacity @retry decorator to provide defaults

    That is just intended to provide some defaults to all tests.
    Usage remains the same as shown in the official docs. Use it when you have a function you want
    to retry:
    ::

        @retry
        def check_uploaded_files():
            result = s3_client.list_objects(Bucket="mybucket", Prefix=prefix)
            assert "Contents" in result
            object_keys = [content["Key"] for content in result["Contents"]]
            assert "ExpectedObjectKey" in object_keys
        check_uploaded_files()

    See: https://github.com/jd/tenacity

    This should be used when we want to check some side effects but they are eventual consistent.

    DON'T retry the whole tests and don't retry request failures. For request failures we should
    investigate the issue.
    """
    all_kwargs = default_retry_strategy()
    all_kwargs.update(kwargs)
    if len(args) == 1 and callable(args[0]):
        return tenacity_retry(**all_kwargs)(args[0])
    return tenacity_retry(**all_kwargs)


class Retrying(TenacityRetrying):
    """Override tenacity Retrying defaults

    That is just intended to provide some defaults to all tests.
    Usage remains the same as shown in the official docs. Use it when you don't want to create a
    function:
    ::

        for attempt in Retrying():
            with attempt:
                assert "Contents" in s3_client.list_objects(Bucket="mybucket", Prefix=prefix)

    See: https://github.com/jd/tenacity#retrying-code-block

    This should be used when we want to check some side effects but they are eventual consistent.

    DON'T retry the whole tests and don't retry request failures. For request failures we should
    investigate the issue.
    """

    def __init__(self, *args, **kwargs):
        all_kwargs = default_retry_strategy()
        all_kwargs.update(kwargs)
        super().__init__(*args, **all_kwargs)


def default_retry_strategy() -> dict:
    """Default arguments to be passed to `@retry` or `Retrying`"""
    return {
        # Only retry on AssertionError because the idea is to retry code that asserts something.
        # Other exceptions are errors, so they should make the test fail fast. If some other
        # exception other than AssertionError is expected, it is better to explicitly use
        # `pytest.raises` in the test:
        # https://docs.pytest.org/en/latest/how-to/assert.html#assertions-about-expected-exceptions
        "retry": retry_if_exception_type(AssertionError),
        # Set a timeout high enough for any eventual consistency to complete
        "stop": stop_after_delay(30),
        "wait": wait_exponential(multiplier=1, min=0.1, max=1),
        # Reraise the underlying AssertionError (or any other exception), otherwise we will get a
        # RetryError as the test failure, which doesn't provide any useful information.
        "reraise": True,
    }


def assert_no_log_entries(
    log_lines: List[str], patterns_to_ignore: Optional[List[re.Pattern]] = None
):
    """Ensure that there are no log entries in the given list

    @param log_lines: the list of log lines to check
    @param patterns_to_ignore: Optional list of patterns to ignore in the logs.
        They will be matched against each entry.
    """
    patterns_to_ignore = patterns_to_ignore or []
    filtered_logs = [
        line for line in log_lines if not any(pattern.match(line) for pattern in patterns_to_ignore)
    ]
    assert filtered_logs == []

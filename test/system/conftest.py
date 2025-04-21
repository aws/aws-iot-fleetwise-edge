# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import logging
import os
import re
import signal
import subprocess
import sys
from pathlib import Path

import pytest
from awscrt import io as awscrt_io
from filelock import FileLock
from tenacity import stop_after_delay

# Make pytest overwrite assert statements inside this module to show the same details as in a test.
# Please note that this needs to be done before the module is imported the first time.
# See https://docs.pytest.org/en/latest/how-to/writing_plugins.html#assertion-rewriting
pytest.register_assert_rewrite("testframework")

# Add testframework/gen to search path to allow standalone usage of protofactory.py
sys.path.insert(0, "testframework/gen")

log = logging.getLogger(__name__)


@pytest.fixture
def tmp_path(request: pytest.FixtureRequest, tmp_path_factory: pytest.TempPathFactory):
    """Redefine pytest's built-in tmp_path fixture to set file permissions and our own naming

    For security reasons, pytest creates the temporary dirs restricting access to the current user
    only: https://github.com/pytest-dev/pytest/issues/10679

    Since we normally run the tests inside docker as root, it means that we wouldn't be able to see
    the files outside docker as a regular user unless we update the permissions after each test run.

    Another thing is that the original tmp_path fixture creates a dir with a short name, which
    can make the dir names very similar when there are tests with the same name:

    https://github.com/pytest-dev/pytest/blob/0ed2d79457b499119693588b71233db957e972c0/src/_pytest/tmpdir.py#L248-L253

    So here we redefine the fixture to set the permissions to the dir and its parents before the
    test starts running and also to use the full test name as the test dir.
    """
    name = re.sub(r"[\W]", "_", request.node.nodeid)
    if os.environ.get("PYTEST_XDIST_WORKER"):
        # When running tests in parallel, there is another dir level with the worker name. That is
        # not very useful to us and makes the path less predictable. Just use the parent dir to make
        # the dir structure the same regardless of whether tests are run in parallel or not.
        tmp_path = tmp_path_factory.getbasetemp().parent / name
        tmp_path.mkdir(parents=True)
    else:
        tmp_path = tmp_path_factory.mktemp(name, numbered=False)
    tmp_path.chmod(0o755)  # Dir with the test name
    tmp_path.parent.chmod(0o755)  # Dir with a test run number (e.g. pytest-3)
    tmp_path.parent.parent.chmod(0o755)  # Dir with the user name (e.g. pytest-of-root)
    return tmp_path


@pytest.fixture(scope="session", autouse=True)
def configure_logging(pytestconfig):
    # Consider built-in pytest log level options.
    # Reference: https://docs.pytest.org/en/latest/how-to/logging.html
    log_level_override = (
        pytestconfig.getoption("--log-cli-level")
        or pytestconfig.getoption("--log-level")
        or pytestconfig.getini("log_level")
    )

    if not log_level_override:
        # When the log isn't explicit set, make it as verbose as practically possible
        logging.getLogger().setLevel(logging.DEBUG)
        # The loggers below are very verbose, so by default don't set DEBUG level. If you want to
        # see more verbose logs for those, pass one of the pytest log level options.
        logging.getLogger("can").setLevel(logging.INFO)
        logging.getLogger("botocore").setLevel(logging.INFO)
        awscrt_io.init_logging(awscrt_io.LogLevel.Warn, "stdout")
    else:
        # If the log level is set, we should just leave it as is (pytest will have already
        # configured the Python loggers). But we still need to configure the awscrt logger.
        python_log_level = {
            "CRITICAL": logging.CRITICAL,
            "FATAL": logging.FATAL,
            "ERROR": logging.ERROR,
            "WARN": logging.WARNING,
            "WARNING": logging.WARNING,
            "INFO": logging.INFO,
            "DEBUG": logging.DEBUG,
            "NOTSET": logging.NOTSET,
        }[log_level_override.upper()]

        awscrt_log_level = {
            logging.FATAL: awscrt_io.LogLevel.Fatal,
            logging.ERROR: awscrt_io.LogLevel.Error,
            logging.WARNING: awscrt_io.LogLevel.Warn,
            logging.INFO: awscrt_io.LogLevel.Info,
            logging.DEBUG: awscrt_io.LogLevel.Debug,
            # NOTSET in Python means log everything, so enable Trace logs
            logging.NOTSET: awscrt_io.LogLevel.Trace,
        }[python_log_level]
        awscrt_io.init_logging(awscrt_log_level, "stdout")


@pytest.fixture(scope="session", autouse=True)
def sigterm_handler():
    """Redirect SIGTERM to SIGINT handler so that pytest shutdown gracefully

    For related discussion see https://github.com/pytest-dev/pytest/issues/5243
    """

    orig = signal.signal(signal.SIGTERM, signal.getsignal(signal.SIGINT))
    yield  # pytest fixture: before is setup, after is tear-down
    signal.signal(signal.SIGTERM, orig)


@pytest.fixture(scope="session")
def worker_number() -> int:
    """Produces a number identifying the current pytest worker

    When running tests in parallel, the pytest-xdist plugin spawns multiple processes and
    distributes a set of tests to each worker.

    Each worker has a name with a sequence number. This fixture returns this sequence number, which
    can then be used to make resource names (e.g. can interface, network namespaces) unique across
    workers and prevent parallel tests from interfering with each other.
    """
    # See https://pytest-xdist.readthedocs.io/en/latest/how-to.html#identifying-the-worker-process-during-a-test # noqa: E501
    worker_name = os.environ.get("PYTEST_XDIST_WORKER", "gw0")
    worker_number = int(worker_name.replace("gw", ""))
    return worker_number


@pytest.fixture(scope="session", autouse=True)
def reset_cangw(configure_logging, tmp_path_factory):
    """Completely reset the CAN gateway only once per run

    Since tests can run in parallel, we need to make sure that the CAN gateway is reset only once.
    Making the fixture session scoped is not enough because tests run in completely separate
    processes.

    So we need to use some lock mechanism to guarantee that only one worker resets the gateway.
    """
    # See https://pytest-xdist.readthedocs.io/en/latest/how-to.html#making-session-scoped-fixtures-execute-only-once # noqa: E501

    from testframework.can_gateway import CanGateway

    worker_name = os.environ.get("PYTEST_XDIST_WORKER")
    if not worker_name:
        # not executing with multiple workers
        CanGateway().reset()
        return

    # get the temp directory shared by all workers
    root_tmp_dir = tmp_path_factory.getbasetemp().parent

    fn = root_tmp_dir / "reset_cangw"
    with FileLock(str(fn) + ".lock"):
        if fn.is_file():
            log.info(
                f"Skipping cangw reset. Another worker already did that: {fn.read_text().strip()}"
            )
        else:
            # create the file and write the worker that reset cangw just to help with debugging
            fn.write_text(worker_name)
            CanGateway().reset()


def pytest_sessionstart(session: pytest.Session):
    if os.environ.get("PYTEST_XDIST_WORKER"):
        return

    session.config.cache.set("fwe/someip_routing_manager_pid", None)


def pytest_sessionfinish(session: pytest.Session):
    """Implement pytest's hook to be executed when the test session ends

    See:

    https://docs.pytest.org/en/latest/reference/reference.html#pytest.hookspec.pytest_sessionfinish
    """
    from testframework.common import Retrying

    if os.environ.get("PYTEST_XDIST_WORKER"):
        return

    # In case the SOME/IP routing manager was ever initialized, we need to kill it here.
    # Normally the clean-up would happen in the fixture that set up the resource. But that wouldn't
    # work when running the tests in parallel.
    # When using pytest-xdist to run tests in parallel, it spawns new Python processes as workers
    # and distributes a set of tests to each one. Any fixture that is used runs in the scope of the
    # worker, so if the worker gets out of work to do, its process will terminate, which will
    # trigger the fixture clean-up.
    # But for the SOME/IP routing manager we need a single routing manager for all tests across all
    # workers. So the process needs to outlive the last running worker. That is why we clean-up
    # (kill the routing manager process) in this hook instead, which runs after all tests have
    # finished.
    pid = session.config.cache.get("fwe/someip_routing_manager_pid", None)
    if pid is None:
        return
    session.config.cache.set("fwe/someip_routing_manager_pid", None)

    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        return

    def routing_manager_finished():
        try:
            subprocess.check_output(["ps", "-p", str(pid)])
            return False
        except subprocess.CalledProcessError:
            return True

    try:
        for attempt in Retrying(stop=stop_after_delay(3)):
            with attempt:
                assert routing_manager_finished()
    except AssertionError:
        # Don't raise an exception because this would cause pytest not to generate the final report
        # It shouldn't normally happen, and we don't really care much if the process didn't die as
        # all tests have finished.
        log.info(f"someip routing manager process {pid} didn't terminate")


@pytest.fixture(scope="session")
def start_someip_routing_manager(
    configure_logging, tmp_path_factory: pytest.TempPathFactory, pytestconfig: pytest.Config
):
    """Start a single process to act as the SOME/IP routing manager for all tests

    When running a SOME/IP application (specifically using vsomeip), the first application creates
    a routing manager. That is a problem for running tests in parallel, because the routing manager
    would die when a test finishes together with the application that started it, which would then
    make other SOME/IP tests fail because they were disconnected from the routing manager.

    So we start a separate process that acts as the routing manager for all tests and kill it only
    when the last test finished (which we need to do in the pytest_sessionfinish hook instead of
    the usual fixture clean-up after yield).
    """
    import subprocess

    from testframework.common import Retrying, is_hil
    from testframework.someip import LOCAL_VSOMEIP_CONFIG

    # If we start the routing manager in the driver machine, it won't work. We would have to run it
    # on the target. But since we don't run the tests in parallel on the target, we don't really
    # bother setting up the routing manager.
    if is_hil():
        return

    def start(log_file, config_file):
        if os.path.exists("/tmp/vsomeip-0"):
            raise RuntimeError(
                "someip routing manager seems to be already running. There is probably a process"
                " that was left running by mistake. Kill the process manually or delete the"
                " /tmp/vsomeip-0 socket file if the process doesn't exist anymore."
            )

        with open(config_file, "w") as fp:
            fp.write(json.dumps(LOCAL_VSOMEIP_CONFIG, indent=4))

        log.info("Starting someip routing manager")
        routing_manager_script = (
            pytestconfig.rootpath / "testframework" / "someip_routing_manager.py"
        )
        environ = os.environ.copy()
        environ["VSOMEIP_CONFIGURATION"] = str(config_file)
        with open(log_file, "w") as log_file_handle:
            someip_routing_manager = subprocess.Popen(
                [sys.executable, routing_manager_script],
                stdout=log_file_handle,
                stderr=subprocess.STDOUT,
                env=environ,
            )

        # Set the pid in the cache, so that is can be killed by pytest-xdist's Controller process
        pytestconfig.cache.set("fwe/someip_routing_manager_pid", someip_routing_manager.pid)
        # We intentionally leak this process because we want it to outlive all pytest-xdist workers.
        # But the Popen.__del__ method causes a warning if the process result haven't been checked:
        # https://github.com/python/cpython/blob/d1a1bca1f0550a4715f1bf32b1586caa7bc4487b/Lib/subprocess.py#L1137
        # Setting the returncode ourselves is a workaround to avoid the warning to be generated.
        someip_routing_manager.returncode = 0
        for attempt in Retrying():
            with attempt:
                assert os.path.exists("/tmp/vsomeip-0"), (
                    f"someip routing manager didn't start. Check the logs in {log_file} or the"
                    " logs for this test"
                )

    worker_name = os.environ.get("PYTEST_XDIST_WORKER")
    if not worker_name:
        # not executing with multiple workers
        log_file = tmp_path_factory.getbasetemp() / "someip_routing_manager.log"
        config_file = tmp_path_factory.getbasetemp() / "someip_routing_manager_config.json"
        start(log_file=log_file, config_file=config_file)
    else:
        # get the temp directory shared by all workers
        root_tmp_dir = tmp_path_factory.getbasetemp().parent

        fn = root_tmp_dir / "start_someip_routing_manager"
        with FileLock(str(fn) + ".lock"):
            if fn.is_file():
                log.info(
                    "Skipping routing manager setup. Another worker already did that:"
                    f" {fn.read_text().strip()}"
                )
                if not os.path.exists("/tmp/vsomeip-0"):
                    raise RuntimeError("someip routing manager didn't start")
            else:
                # create the file and write the worker that started it just to help with debugging
                fn.write_text(worker_name)
                log_file = root_tmp_dir / "someip_routing_manager.log"
                config_file = tmp_path_factory.getbasetemp() / "someip_routing_manager_config.json"
                start(log_file=log_file, config_file=config_file)


@pytest.fixture
def someipigen(tmp_path: Path, start_someip_routing_manager, worker_number: int):
    """Start the SOME/IP SignalManager (a.k.a someipigen) to generate data for SOME/IP interface

    Note that this depends on a routing manager to be initialized separately, otherwise parallel
    tests may interfere with each other.
    """
    from someipigen import SignalManager  # noqa:
    from testframework.someip import create_someipigen_config

    vsomeip_config_file = create_someipigen_config(tmp_path, worker_number)
    # Set the environment variable for someipigen:
    os.environ["VSOMEIP_CONFIGURATION"] = vsomeip_config_file

    someipigen_instance = f"commonapi.ExampleSomeipInterface{worker_number}"
    log.info(f"Starting someipigen instance {someipigen_instance}")
    someipigen = SignalManager()
    someipigen.start("local", someipigen_instance, "someipigen")

    yield someipigen

    someipigen.stop()


@pytest.fixture
def someip_device_shadow_editor(tmp_path: Path, start_someip_routing_manager, worker_number: int):
    """Start the SOME/IP Device Shadow Editor

    Note that this depends on a routing manager to be initialized separately, otherwise parallel
    tests may interfere with each other.
    """
    from someip_device_shadow_editor import DeviceShadowOverSomeipExampleApplication
    from testframework.someip import create_device_someip_shadow_editor_config

    vsomeip_config_file = create_device_someip_shadow_editor_config(tmp_path, worker_number)
    # Set the environment variable for someip_device_shadow_editor:
    os.environ["VSOMEIP_CONFIGURATION"] = vsomeip_config_file

    someip_device_shadow_editor_instance = (
        f"commonapi.DeviceShadowOverSomeipInterface{worker_number}"
    )
    log.info(
        f"Starting someip device shadow editor instance {someip_device_shadow_editor_instance}"
    )
    someip_device_shadow_editor = DeviceShadowOverSomeipExampleApplication()
    someip_device_shadow_editor.init(
        "local", someip_device_shadow_editor_instance, "someip_device_shadow_editor"
    )

    yield someip_device_shadow_editor

    someip_device_shadow_editor.deinit()


@pytest.fixture
def can_to_someip(tmp_path: Path, start_someip_routing_manager, worker_number: int):
    """Start the CAN to SOME/IP process to redirect data from CAN to SOME/IP interface

    Note that this depends on a routing manager to be initialized separately, otherwise parallel
    tests may interfere with each other.
    """
    from testframework.common import is_hil
    from testframework.someip import create_can_to_someip_config

    if is_hil():
        can_interface = os.environ["TEST_HIL_DRIVER_CAN_INTERFACE"]
    else:
        can_interface = f"vcan{worker_number}"

    someip_instance_id_can = f"0x56{worker_number:02x}"
    vsomeip_cfg_file_can_to_someip = create_can_to_someip_config(tmp_path, someip_instance_id_can)

    cmd = (
        f"VSOMEIP_CONFIGURATION={vsomeip_cfg_file_can_to_someip}"
        f" {os.environ['TEST_CAN_TO_SOMEIP_BINARY']}"
        f" --can-interface {can_interface}"
        f" --instance-id {someip_instance_id_can}"
    )

    can_to_someip_process = subprocess.Popen(cmd, shell=True, preexec_fn=os.setsid)

    yield someip_instance_id_can

    os.killpg(os.getpgid(can_to_someip_process.pid), signal.SIGTERM)
    can_to_someip_process.wait(timeout=30)

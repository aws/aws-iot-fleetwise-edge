# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import abc
import contextlib
import logging
import os
import subprocess
import sys
import textwrap
from abc import abstractmethod
from pathlib import Path
from signal import Signals
from typing import Dict, List, Optional, Tuple, Union

from tenacity import retry_if_exception_type
from testframework.common import Retrying, is_hil

log = logging.getLogger(__name__)


class ProcessWrapper(abc.ABC):
    """An interface for processes spawned by the subprocess helper

    That is mainly intended to allow interacting with remote processes as if they were local.
    """

    pid: int

    @abstractmethod
    def terminate(self):
        """Try to gracefully terminate the process without waiting for it

        Note that this is not the same as kill.
        """

    @abstractmethod
    def kill(self):
        """Forcefully terminate the process without waiting for it"""

    @abstractmethod
    def wait(self, timeout: int) -> int:
        """Wait for the process to terminate

        Returns the exit code of the process
        """


class TargetProcess(ProcessWrapper):
    """A process that runs on the target machine

    Note that the target can be the same as the local machine, depending on whether this is a HIL
    setup or not.

    This is intended to be used similar to a regular Popen object, but the caller doesn't need to
    know about the details on how to terminate or get the PID of a remote process.
    """

    def __init__(self, subprocess_helper, args: Union[str, List[str]], executable: str, **kwargs):
        # We need start_new_session=True to prevent the child process from receiving a SIGINT when
        # we cancel the tests with Ctrl + C or send a SIGTERM to the pytest process. In such case
        # we don't want subprocesses to be killed automatically. This will be done as part of the
        # normal teardown workflow.
        # See https://stackoverflow.com/a/23839524
        self._process = subprocess.Popen(args, start_new_session=True, **kwargs)
        self._subprocess_helper = subprocess_helper
        if is_hil():
            # It may take a while for the remote process to start, so wait until we get its PID
            for attempt in Retrying(retry=retry_if_exception_type(subprocess.CalledProcessError)):
                with attempt:
                    self.pid = int(
                        self._subprocess_helper.target_check_output(
                            ["pgrep", "-f", f"^{executable}"]
                        ).strip()
                    )
        # For wrapper commands, which run the main command as a child, we want the child PID
        elif "sudo" in args or "faketime" in args:
            for attempt in Retrying(retry=retry_if_exception_type(subprocess.CalledProcessError)):
                with attempt:
                    self.pid = int(
                        self._subprocess_helper.local_check_output(
                            ["pgrep", "-P", str(self._process.pid), "-f", str(executable)]
                        ).strip()
                    )
        else:
            self.pid = self._process.pid

    def terminate(self):
        self._subprocess_helper.target_check_call(["kill", "-s", "TERM", str(self.pid)])

    def kill(self):
        self._subprocess_helper.target_check_call(["kill", "-s", "KILL", str(self.pid)])

    def wait(self, timeout: int) -> int:
        return self._process.wait(timeout=timeout)


class SubprocessHelper:
    """An abstraction around Python's subprocess module to make it easier to run commands on HIL

    Most methods have a local_ or target_ prefix. Local means the same machine as the test itself
    and target is where FWE is running (which may be the same machine or a remote one).
    """

    def __init__(self, *, as_root_user: bool):
        self._as_root_user = as_root_user
        self._is_hil = is_hil()
        self._ssh_command_prefix = []

        if self._is_hil:
            self._ssh_target = f'{os.environ["BOARD_USERNAME"]}@{os.environ["BOARD_IP_ADDRESS"]}'
            self._ssh_command_prefix = [
                "ssh",
                "-i",
                "~/.ssh/board_rsa",
                self._ssh_target,
            ]

    def local_popen(self, args: List[str], **kwargs):
        full_command, kwargs = self._local_args(args, **kwargs)
        log.info(f"Starting process: {self.args_to_string(full_command)}")
        return subprocess.Popen(args, start_new_session=True, **kwargs)

    def local_check_output(self, args: List[str], **kwargs) -> str:
        full_command, kwargs = self._local_args(args, **kwargs)
        return self._check_output(full_command, **kwargs)

    def local_check_call(self, args: List[str], **kwargs):
        full_command, kwargs = self._local_args(args, **kwargs)
        return self._check_call(full_command, **kwargs)

    def target_popen(self, args: List[str], *, executable: str, **kwargs) -> ProcessWrapper:
        full_command, kwargs = self._target_args(args, **kwargs)
        log.info(f"Starting process: {self.args_to_string(full_command)}")
        return TargetProcess(self, full_command, executable=executable, **kwargs)

    def target_check_output(self, args: List[str], **kwargs) -> str:
        full_command, kwargs = self._target_args(args, **kwargs)
        return self._check_output(full_command, **kwargs)

    def target_check_call(self, args: List[str], **kwargs):
        full_command, kwargs = self._target_args(args, **kwargs)
        return self._check_call(full_command, **kwargs)

    def _check_output(self, args: Union[str, List[str]], **kwargs) -> str:
        log.info(f"Running command: {self.args_to_string(args)}")
        # The output is returned and stderr will go to pytest's captured stderr. So print the
        # command to stderr to identify the source when investigating failures.
        print(f"\n\n{self.args_to_string(args)}", file=sys.stderr)
        return subprocess.check_output(args, **kwargs).decode("utf-8")

    def _check_call(self, args: Union[str, List[str]], **kwargs):
        log.info(f"Running command: {self.args_to_string(args)}")
        # Since with check_call the output goes to stdout, print the command to identify the source
        print(f"\n\n{self.args_to_string(args)}")
        # Redirect stderr to stdout so that they both appear together in pytest's captured output
        return subprocess.check_call(args, stderr=subprocess.STDOUT, **kwargs)

    def _local_args(self, args: List[str], **kwargs) -> Tuple[Union[str, List[str]], Dict]:
        full_command = args
        if self._as_root_user:
            full_command = ["sudo", "--preserve-env", "--preserve-env=LD_LIBRARY_PATH"] + args

        if kwargs.get("shell"):
            full_command = self.args_to_string(full_command)

        # We usually run the tests setting TMPDIR to change the temp dir used by Python and
        # thus pytest. That is just to make the test artifacts easier to find. But when
        # launching other processes we normally want to use the system's default temp dir.
        if "env" in kwargs:
            kwargs["env"] = kwargs["env"].copy()
        else:
            kwargs["env"] = os.environ.copy()
        kwargs["env"].pop("TMPDIR", None)

        return full_command, kwargs

    def _target_args(self, args: List[str], **kwargs) -> Tuple[Union[str, List[str]], Dict]:
        if "env" in kwargs:
            raise ValueError(
                "'env' arg shouldn't be passed to target processes because 'env' should contain all"
                " env vars inherited by a process. When the target process is remote, it should"
                " instead inherit the env vars from the remote machine."
                " Use 'extra_env' to override only the env vars you need."
            )

        full_command = args
        if self._as_root_user:
            full_command = ["sudo", "--preserve-env", "--preserve-env=LD_LIBRARY_PATH"] + args

        extra_env = kwargs.pop("extra_env", {})
        if self._ssh_command_prefix:
            env_vars = ""
            # For ssh we need to set the env vars in the command line.
            for key, value in extra_env.items():
                env_vars += f"{key}={self.args_to_string([value])} "
            if env_vars:
                env_vars = "env " + env_vars

            full_command = self._ssh_command_prefix + [env_vars + self.args_to_string(full_command)]
            # With ssh, there is no need to run as shell
            kwargs.pop("shell", None)
        else:
            # Since this is not ssh, we can't just pass the env vars we want to override. We have to
            # copy the whole environ and update it with the ones we need to change.
            kwargs["env"] = os.environ.copy()
            # We usually run the tests setting TMPDIR to change the temp dir used by Python and
            # thus pytest. That is just to make the test artifacts easier to find. But when
            # launching other processes we normally want to use the system's default temp dir.
            kwargs["env"].pop("TMPDIR", None)
            kwargs["env"].update(extra_env)
            if kwargs.get("shell"):
                full_command = self.args_to_string(full_command)

        return full_command, kwargs

    def target_find_process_id_by_executable(self, executable: str, ancestor: int) -> Optional[int]:
        # It may take a while for the remote process to start, so wait until we get its PID
        for attempt in Retrying(
            retry=retry_if_exception_type((subprocess.CalledProcessError, AssertionError))
        ):
            with attempt:
                matching_pids = (
                    self.target_check_output(["pgrep", "-f", f"^{executable}"]).strip().splitlines()
                )
                for pid in matching_pids:
                    pid = int(pid)
                    if pid == ancestor or self.target_is_process_descendent(pid, ancestor):
                        return pid

        return None

    def target_is_process_descendent(self, pid: int, target_ancestor: int):
        while pid != 1:
            parent = int(self.target_check_output(["ps", "-o", "ppid=", str(pid)]).strip())
            if parent == target_ancestor:
                return True
            pid = parent

        return False

    def args_to_string(self, args: Union[str, List[str]]) -> str:
        if isinstance(args, str):
            return args

        quoted_args = []
        for arg in args:
            arg = str(arg)
            if " " in arg:
                arg = f'"{arg}"'
            quoted_args.append(arg)
        return " ".join(quoted_args)

    def copy_files_from_target(self, source: Union[str, Path], destination: Union[str, Path]):
        if not self._is_hil:
            return

        remote_source = f"{self._ssh_target}:{source}"
        self.local_check_call(
            [
                "scp",
                "-i",
                "~/.ssh/board_rsa",
                "-r",
                remote_source,
                str(destination),
            ]
        )

    def copy_files_to_target(self, source: List[Union[str, Path]], destination: Union[str, Path]):
        if not self._is_hil:
            return

        remote_destination = f"{self._ssh_target}:{destination}"
        self.local_check_call(
            [
                "scp",
                "-i",
                "~/.ssh/board_rsa",
                "-r",
                *[str(path) for path in source],
                remote_destination,
            ]
        )


class Perf:
    def __init__(self, subprocess_helper: SubprocessHelper, pid: int, output_svg_file: Path):
        self._subprocess_helper = subprocess_helper
        self._base_dir = output_svg_file.parent
        self._output_svg_file = output_svg_file
        self._output_file_basename = os.path.splitext(output_svg_file.name)[0]
        self._output_data_file = self._base_dir / f"{self._output_file_basename}.data"
        self._perf_executable = self._get_perf_executable()
        cmd_list = [
            self._perf_executable,
            "record",
            "-o",
            str(self._output_data_file),
            "-F",
            "99",
            "-g",
            "-p",
            str(pid),
        ]
        self._process = self._subprocess_helper.target_popen(
            cmd_list, executable=self._perf_executable
        )

    def detach(self):
        self._process.terminate()
        self._process.wait(timeout=30)
        trace_output_file = self._base_dir / f"{self._output_file_basename}_trace_output"
        perf_folded_file = self._base_dir / f"{self._output_file_basename}.perf-folded"
        self._subprocess_helper.target_check_call(
            [
                self._perf_executable,
                "script",
                "-v",
                "-i",
                str(self._output_data_file),
                ">",
                str(trace_output_file),
            ],
            shell=True,
        )
        self._subprocess_helper.copy_files_from_target(trace_output_file, self._base_dir)
        self._subprocess_helper.local_check_call(
            [
                "cat",
                str(trace_output_file),
                "|",
                "/usr/local/FlameGraph/stackcollapse-perf.pl",
                ">",
                str(perf_folded_file),
            ],
            shell=True,
        )
        self._subprocess_helper.local_check_call(
            [
                "/usr/local/FlameGraph/flamegraph.pl",
                str(perf_folded_file),
                ">",
                str(self._output_svg_file),
            ],
            shell=True,
        )

    def _get_perf_executable(self) -> str:
        if is_hil():
            return "perf"

        # Since perf is tightly integrated with the Linux kernel, normally the perf command is a
        # wrapper script that tries to get the perf tools for the specific kernel version we are
        # running. Since when running a docker container, the kernel is provided by the host,
        # normally the wrapper won't find the right version.
        # We can still use the version installed in the docker container, although we will
        # probably won't be able to use features from the latest kernels. That is why we have to
        # search for the specific version that is installed.
        perf_binaries = list(Path("/").glob("usr/lib/linux-tools/*/perf"))
        if not perf_binaries:
            raise RuntimeError("No perf binary found")
        if len(perf_binaries) > 1:
            log.warning(f"Multiple perf binaries found, using the first one: {perf_binaries}")
        return str(perf_binaries[0])


@contextlib.contextmanager
def attach_perf(subprocess_helper: SubprocessHelper, pid: int, output_svg_file: Path):
    """Attach perf to the given process and generate an SVG flame graph when the context exits"""

    perf = Perf(subprocess_helper, pid, output_svg_file)
    try:
        yield
    finally:
        perf.detach()


def stop_process_and_wait(
    subprocess_helper: SubprocessHelper,
    process: Union[subprocess.Popen, ProcessWrapper],
    core_dump_dir: Path,
    timeout=30,
    is_valgrind=False,
):
    """Tries to gracefully terminate the given process or force termination if necessary

    In case it needs to force termination, it tries to get debug info before killing the process.
    """

    process_return_code = None

    executable_name = subprocess_helper.target_check_output(
        ["readlink", "-f", f"/proc/{process.pid}/exe"]
    ).strip()

    log.info(f"Sending SIGTERM to process {executable_name} with PID {process.pid}")
    process.terminate()
    try:
        process_return_code = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            generate_debug_info(subprocess_helper, process, core_dump_dir, is_valgrind)
        finally:
            log.error(
                f"Sending SIGKILL to process {executable_name} with PID {process.pid} as it"
                " did not react to SIGTERM"
            )
            process.kill()
        process.wait(timeout=timeout)

    return process_return_code


# This script is used to print the stacktrace of all threads in a process multiple times.
# Normally we can just print the stacktraces with -ex "thread apply all bt", but our code calls gdb
# when the process stops responding and needs to be force killed.
#
# In such case it may be helpful to print the stacktraces, continue for some time, interrupt and
# print again. This can show which threads are still making progress and which ones are really
# stuck.
#
# The way the script does that is by using gdb.post_event function, which is asynchronously called
# by gdb.
#
# In more modern gdb versions we could use threads and gdb.interrupt(), but those are not available
# in the version we are currently using:
# https://sourceware.org/gdb/current/onlinedocs/gdb.html/Threading-in-GDB.html
_gdb_script_print_stacktrace = textwrap.dedent(
    """
    python

    import random
    import time

    def interrupt():
        time.sleep(3 + random.random() * 2)
        print("interrupting")
        gdb.execute("interrupt")
        gdb.post_event(interrupt)

    gdb.post_event(interrupt)

    for i in range(5):
        print("continuing")
        gdb.execute("continue")
        print("-" * 80)
        gdb.execute("thread apply all bt")
        print("-" * 80)
        i += 1
    """
).strip()


def generate_debug_info(
    subprocess_helper: SubprocessHelper,
    process: Union[subprocess.Popen, ProcessWrapper],
    core_dump_dir: Path,
    is_valgrind: bool,
):
    executable_name = subprocess_helper.target_check_output(
        ["readlink", "-f", f"/proc/{process.pid}/exe"]
    ).strip()
    log.error(
        f"Printing stacktrace of all threads for process {executable_name} with PID"
        f" {process.pid} (see the captured stdout)"
    )
    # We could use gcore to generate a core dump, but when using valgrind it doesn't
    # produce a useful dump. Normally we would need the vgcore files instead, but
    # valgrind only creates them when there is a crash. So we use gdb to at least print
    # the stack traces and generate the core dump when possible.
    gdb_command = [
        "gdb",
        "-batch",
        "-ex",
        "set auto-load safe-path /",
    ]
    if is_valgrind:
        gdb_command += ["-ex", f"target remote | vgdb --pid={process.pid}"]
    else:
        log.error(
            f"Generating core dump for process {executable_name} with PID {process.pid}"
            " (see the captured stdout)"
        )
        # generate-core-file command doesn't work with valgrind
        core_dump_file_path = core_dump_dir / f"core_fwe.{process.pid}"
        gdb_command += [
            "-p",
            str(process.pid),
            "-ex",
            "generate-core-file",
            str(core_dump_file_path),
        ]
    gdb_command += ["-ex", _gdb_script_print_stacktrace]
    try:
        subprocess_helper.target_check_call(gdb_command)
    except subprocess.CalledProcessError as e:
        # When scripting gdb using Python, it sometimes aborts after running the Python code.
        # That is probably because there is still some of our code running since we post async
        # events for gdb to run. But that doesn't matter much since at this point we already got the
        # stacktraces we wanted. So we ignore any SIGABRT from the gdb process.
        if -(e.returncode) == Signals.SIGABRT.value:
            log.info("Ignoring SIGABRT from gdb. This can happen when using Python to script gdb")
        else:
            raise

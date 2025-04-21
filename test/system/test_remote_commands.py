# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import random
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional

import pytest
from tenacity import stop_after_delay
from testframework.common import Retrying
from testframework.context import Context
from testframework.gen import command_response_pb2
from testframework.protofactory import SignalType

log = logging.getLogger(__name__)


class TestRemoteCommands:
    @pytest.fixture(autouse=True, params=[{}], ids=["default_commands_context"])
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int, someipigen):
        context_kwargs = request.param
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            someip_commands_enabled=True,
            can_commands_enabled=True,
            someipigen_instance=someipigen.get_instance(),
            **context_kwargs,
        )
        self.context.start_can_command_server()

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_can_command_server()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def check_execute_command(
        self,
        actuator_name,
        value,
        signal_type,
        get_value=None,
        timeout: Optional[timedelta] = None,
        issued_time: Optional[datetime] = None,
        max_delay_allowed=2.5,
        expected_value=None,
        expected_final_command_status=command_response_pb2.COMMAND_STATUS_SUCCEEDED,
    ):
        timeout = timeout or timedelta(seconds=10)
        expected_value = expected_value if expected_value is not None else value
        command_id = str(random.getrandbits(16))
        if get_value is not None:
            assert get_value(command_id) != value, (
                "Current value already set to command value,"
                " not possible to verify command execution"
            )

        self.context.send_actuator_command_request(
            command_id,
            actuator_name,
            value,
            signal_type,
            timeout,
            issued_time=issued_time,
        )

        log.info("Wait for the command response")
        # Our target for E2E latency is 4 seconds, so set a smaller retry timeout to ensure FWE is
        # not delaying things too much
        for attempt in Retrying(stop=stop_after_delay(max_delay_allowed)):
            with attempt:
                assert (
                    len(self.context.received_command_responses) >= 1
                ), "Response with final command status didn't arrive within the expected interval"
                command_response = self.context.received_command_responses.pop(0)
                assert command_response.status != command_response_pb2.COMMAND_STATUS_IN_PROGRESS

        assert command_response.status == expected_final_command_status
        assert command_response.command_id == command_id
        if get_value is not None:
            assert get_value(command_id) == expected_value

        return command_response

    # Execute multiple commands in parallel. concurrent_command_ids defines number of
    # parallel executions
    def check_concurrent_execute_command(
        self,
        actuator_name,
        get_value,
        value,
        signal_type,
        no_of_concurrent_commands,
        timeout: Optional[timedelta] = None,
        max_delay_allowed=15,
    ):

        concurrent_command_ids = []
        timeout = timeout or timedelta(seconds=11)

        for _ in range(no_of_concurrent_commands):
            command_id = str(random.getrandbits(64))
            concurrent_command_ids.append(command_id)
            assert get_value(command_id) in [0, ""]

            # Send commands in loop
            self.context.send_actuator_command_request(
                command_id, actuator_name, value, signal_type, timeout
            )

        log.info("Wait for the command response")

        received_statuses = {ids: [] for ids in concurrent_command_ids}
        for attempt in Retrying(stop=stop_after_delay(max_delay_allowed)):
            with attempt:
                assert (
                    len(self.context.received_command_responses) >= 1
                ), "No response arrived within the expected interval"

                # Parse the response and fill the tracker dictionaries mapped to command ID

                while self.context.received_command_responses:
                    response = self.context.received_command_responses.pop(0)
                    print(
                        f"Received command response: command_id: {response.command_id}, "
                        f"status: {response.status}"
                    )
                    received_statuses[response.command_id].append(response.status)

                for _, statuses in received_statuses.items():
                    assert statuses == ([command_response_pb2.COMMAND_STATUS_IN_PROGRESS] * 10) + [
                        command_response_pb2.COMMAND_STATUS_SUCCEEDED
                    ]

        assert get_value(concurrent_command_ids[-1]) == value

    def test_simple_commands(self, someipigen):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        self.check_execute_command(
            "Vehicle.actuator1",
            123,
            SignalType.INT32,
            get_value=lambda cmd_id: someipigen.get_value("Int32"),
        )
        self.check_execute_command(
            "Vehicle.actuator2",
            456,
            SignalType.INT64,
            get_value=lambda cmd_id: someipigen.get_value("Int64"),
        )
        self.check_execute_command(
            "Vehicle.actuator3",
            True,
            SignalType.BOOL,
            get_value=lambda cmd_id: someipigen.get_value("Boolean"),
        )
        self.check_execute_command(
            "Vehicle.actuator4",
            789.0,
            SignalType.FLOAT32,
            get_value=lambda cmd_id: someipigen.get_value("Float"),
        )
        self.check_execute_command(
            "Vehicle.actuator5",
            9999.0,
            SignalType.FLOAT64,
            get_value=lambda cmd_id: someipigen.get_value("Double"),
        )
        self.check_execute_command(
            "Vehicle.actuator6",
            789,
            SignalType.INT32,
            get_value=lambda cmd_id: self.context.can_command_server.args.get(cmd_id, 0),
        )
        self.check_execute_command(
            "Vehicle.actuator9",
            "abcd",
            SignalType.STRING,
            get_value=lambda cmd_id: someipigen.get_value("String"),
        )

        someipigen.set_value("Int64", 3443)
        command_response = self.check_execute_command(
            "Vehicle.actuator2",
            456,
            SignalType.INT64,
            get_value=lambda cmd_id: someipigen.get_value("Int64"),
            issued_time=datetime.now() - timedelta(seconds=100),  # already expired
            expected_value=3443,
            expected_final_command_status=command_response_pb2.COMMAND_STATUS_EXECUTION_TIMEOUT,
        )
        # REASON_CODE_TIMED_OUT_BEFORE_DISPATCH
        assert command_response.reason_code == 0x12

        someipigen.set_value("Int64", 3443)
        command_response = self.check_execute_command(
            "Vehicle.actuator1",
            value=456,
            signal_type=SignalType.INT64,
            get_value=lambda cmd_id: someipigen.get_value("Int64"),
            expected_value=3443,
            expected_final_command_status=command_response_pb2.COMMAND_STATUS_EXECUTION_FAILED,
        )
        # REASON_CODE_ARGUMENT_TYPE_MISMATCH
        assert command_response.reason_code == 0x7

    def test_someipigen_instance_unavailable(self, someipigen):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        someipigen.set_value("Int64", 3443)
        someipigen.stop()
        command_response = self.check_execute_command(
            "Vehicle.actuator2",
            value=456,
            signal_type=SignalType.INT64,
            expected_final_command_status=command_response_pb2.COMMAND_STATUS_EXECUTION_FAILED,
        )
        # REASON_CODE_UNAVAILABLE
        assert command_response.reason_code == 0xE

    @pytest.mark.parametrize(
        "setup",
        [{"persistency_file": True, "session_expiry_interval_seconds": 300}],
        ids=str,
        indirect=True,
    )
    def test_offline_commands(self, someipigen):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        self.context.stop_fwe()
        self.context.connect_to_cloud()

        log.info("FWE stopped. Sending command while offline.")
        command_id = str(random.getrandbits(16))
        self.context.send_actuator_command_request(
            command_id,
            "Vehicle.actuator1",
            123,
            SignalType.INT32,
            timeout=timedelta(seconds=60),
        )

        # With persistent session enabled, FWE should re-join the previous session and receive all
        # messages from topics that it was subscribed before.
        self.context.start_fwe()

        log.info("Wait for the command response")
        for attempt in Retrying(stop=stop_after_delay(15)):
            with attempt:
                assert (
                    len(self.context.received_command_responses) >= 1
                ), "Response with final command status didn't arrive within the expected interval"
                command_response = self.context.received_command_responses.pop(0)
                assert command_response.status != command_response_pb2.COMMAND_STATUS_IN_PROGRESS

        assert command_response.status == command_response_pb2.COMMAND_STATUS_SUCCEEDED
        assert command_response.command_id == command_id
        assert someipigen.get_value("Int32") == 123

    def test_long_running_commands(self, someipigen):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        self.check_execute_command(
            "Vehicle.actuator7",
            123.456,
            SignalType.FLOAT64,
            get_value=lambda cmd_id: self.context.can_command_server.args.get(cmd_id, 0),
            timeout=timedelta(seconds=12),
            max_delay_allowed=15,
        )
        self.check_execute_command(
            "Vehicle.actuator20",
            128,
            SignalType.INT32,
            get_value=lambda cmd_id: someipigen.get_value("Int32LRC"),
            timeout=timedelta(seconds=12),
            max_delay_allowed=15,
        )

        command_response = self.check_execute_command(
            "Vehicle.actuator20",
            32,
            SignalType.INT32,
            get_value=lambda cmd_id: someipigen.get_value("Int32LRC"),
            timeout=timedelta(seconds=3),
            max_delay_allowed=6,
            issued_time=datetime.now(),
            expected_value=128,
            # This is a timeout on SOME/IP side. CommonAPI doesn't provide a specific status for
            # timeout. It returns a REMOTE_ERROR, so FWE can't translate this to a command status
            # timeout.
            # https://github.com/COVESA/capicxx-core-runtime/blob/c4351ee6972890b225ec9d4984c8f4fa9dd09ae8/include/CommonAPI/Types.hpp#L25-L35
            expected_final_command_status=command_response_pb2.COMMAND_STATUS_EXECUTION_FAILED,
        )
        assert command_response.reason_code >= 0x00010000  # REASON_CODE_OEM_RANGE_START
        assert command_response.reason_code <= 0x0001FFFF  # REASON_CODE_OEM_RANGE_END
        assert command_response.reason_description == "REMOTE_ERROR"

    def test_concurrent_command(self, someipigen):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        self.check_concurrent_execute_command(
            "Vehicle.actuator20",
            lambda cmd_id: someipigen.get_value("Int32LRC"),
            128,
            SignalType.INT32,
            no_of_concurrent_commands=3,
            timeout=timedelta(seconds=15),
            max_delay_allowed=15,
        )

    def test_decoder_manifest_out_of_sync_command(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        self.context.send_actuator_command_request(
            "command123",
            "Vehicle.actuator1",
            123,
            SignalType.INT32,
            timedelta(seconds=10),
            "wrong_decoder_manifest_id",
        )

        log.info("Wait for the command response")
        # Our target for E2E latency is 4 seconds, so set a smaller retry timeout to ensure FWE is
        # not delaying things too much
        for attempt in Retrying(stop=stop_after_delay(2.5)):
            with attempt:
                assert (
                    len(self.context.received_command_responses) == 1
                ), "No response arrived within the expected interval"

        command_response = self.context.received_command_responses[0]
        assert command_response.command_id == "command123"
        assert command_response.status == command_response_pb2.COMMAND_STATUS_EXECUTION_FAILED
        assert command_response.reason_code == 2  # DECODER_MANIFEST_OUT_OF_SYNC

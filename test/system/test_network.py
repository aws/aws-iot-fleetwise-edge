# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from collections import Counter
from datetime import timedelta
from io import TextIOWrapper
from pathlib import Path
from threading import Thread

import pytest
from tenacity import stop_after_delay, wait_fixed
from testframework.common import Retrying
from testframework.context import AwsSdkLogLevel, Context
from testframework.gen import command_response_pb2
from testframework.protofactory import SignalType

log = logging.getLogger(__name__)


class TestNetworkCollectionScheme:
    """Test recovery from network loss"""

    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int, someipigen):
        self.context_kwargs = request.param
        # The IoT Core server doesn't accept anything lower than 30 seconds
        self.keep_alive_interval_seconds = 30
        self.ping_timeout_seconds = 3
        self.disconnection_detection_seconds = (
            self.keep_alive_interval_seconds + self.ping_timeout_seconds
        )
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            enable_network_namespace=True,
            keep_alive_interval_seconds=self.keep_alive_interval_seconds,
            ping_timeout_ms=self.ping_timeout_seconds * 1000,
            # These tests are the most likely to hit some corner cases with the MQTT connection, so
            # increase verbosity.
            aws_sdk_log_level=AwsSdkLogLevel.Trace,
            someipigen_instance=someipigen.get_instance(),
            **self.context_kwargs,
        )

        self.HEARTBEAT_PERIOD_MS = 1000
        self.collection_schemes = [
            {
                "campaignSyncId": "heartbeat",
                "collectionScheme": {
                    "timeBasedCollectionScheme": {"periodMs": self.HEARTBEAT_PERIOD_MS}
                },
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Main_Light_Switch"},
                    {"name": "Throttle__Position"},
                ],
                "spoolingMode": "TO_DISK",
                "compression": "SNAPPY",
            }
        ]
        self.stop = False
        self.thread = None

        yield  # pytest fixture: before is setup, after is tear-down

        self.stop = True
        if self.thread:
            self.thread.join()
        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def wait_for_fwe_disconnection(self, fwe_log: TextIOWrapper):
        log.info(
            f"Wait up to {self.disconnection_detection_seconds} seconds for FWE to detect"
            " dropped internet"
        )

        incomplete_log_line = ""
        # Wait for the ping timeout to appear in the logs to really ensure that FWE detect the
        # network is down. This is preferable to using a fixed sleep because we don't have a
        # guarantee that the MQTT client will use the exact keep alive interval we passed (e.g.
        # the actual value may be negotiated with the server and the current minimum value accepted
        # by the server is 30 seconds).
        for attempt in Retrying(stop=stop_after_delay(self.disconnection_detection_seconds)):
            with attempt:
                output = incomplete_log_line + fwe_log.read()
                last_newline = output.rfind("\n")
                if last_newline == -1:
                    incomplete_log_line = output
                    output = ""
                else:
                    incomplete_log_line = output[last_newline + 1 :]
                    output = output[: last_newline + 1]
                assert "AWS_ERROR_MQTT5_PING_RESPONSE_TIMEOUT" in output, (
                    "FWE didn't detect that the network is down within the expected interval. This"
                    " might happen if the server negotiated a larger keep alive than the one we"
                    " tried to set."
                )

    @pytest.mark.parametrize(
        "setup",
        [
            # Test with both persistent session enabled and disabled since this changes the
            # behavior on reconnection. When persistent session is disabled, the client doesn't
            # automatically resubscribe to the topics.
            {"persistency_file": False, "session_expiry_interval_seconds": 0},
            {"persistency_file": False, "session_expiry_interval_seconds": 300},
        ],
        ids=str,
        indirect=True,
    )
    def test_network_link_down(self, request: pytest.FixtureRequest):
        """Start without internet connection then enable internet, then disconnect again.

        After reconnect check that old messages were thrown away and not transmitted.
        """
        self.context.start_cyclic_can_messages()
        self.context.link_throttle(delay_ms=10)
        self.context.connect_to_cloud()

        self.context.link_down()
        self.context.start_fwe(wait_for_startup=False)
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(self.collection_schemes)

        log.info("Wait 14 seconds as FWE has no internet nothing should arrive")
        time.sleep(14)
        assert len(self.context.received_data) == 0

        self.context.set_can_signal("Main_Light_Switch", 0)

        self.context.link_up()

        log.info("Wait to give chance to connect first time")
        for attempt in Retrying(
            stop=stop_after_delay(self.keep_alive_interval_seconds + 5), wait=wait_fixed(1)
        ):
            with attempt:
                self.context.send_decoder_manifest()
                self.context.send_collection_schemes(self.collection_schemes)
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        "heartbeat",
                    }
                )

        log.info("After collection scheme and decoder manifest, wait 15 seconds for data")
        for attempt in Retrying(stop=stop_after_delay(15)):
            with attempt:
                assert len(self.context.received_data) > 0

        fwe_log = open(self.context.fwe_logfile_path)
        request.addfinalizer(fwe_log.close)
        # Advance the read pointer so that we can poll only the recent lines
        fwe_log.read()

        self.context.link_down()

        self.wait_for_fwe_disconnection(fwe_log)
        self.context.received_data = []

        log.info(
            "Changing signal value to 1 while link is down."
            " All data with this value should dropped."
        )

        self.context.set_can_signal("Main_Light_Switch", 1)
        time.sleep(self.HEARTBEAT_PERIOD_MS / 1000 * 5)
        assert len(self.context.received_data) == 0

        # Update the signal value before making the link up again. Only this value should be
        # received. The previous one should all have been dropped because the connection was down
        # and persistency off.
        self.context.set_can_signal("Main_Light_Switch", 2)
        log.info(
            "Waiting after changing signal value to 2 to ensure that FWE drops all data with value"
            " 1 before restoring the network"
        )
        time.sleep(self.HEARTBEAT_PERIOD_MS / 1000 * 5)
        self.context.link_up()
        log.info(
            "We assume sdk throws away all messages not delivered after detecting connection is"
            " down. This probably only works if kernel queue does not get filled. This would result"
            " in EGAIN and the sdk buffer data in the in heap"
        )

        # We need to give more time because it includes the time for FWE to reconnect and send the
        # heartbeat data.
        received_data_after_link_up = []
        value_counter = Counter()
        for attempt in Retrying(stop=stop_after_delay(60)):
            with attempt:
                log.info(f"Waiting for value 2. Current value count after link up: {value_counter}")
                # Wait until we start getting enough 2's. Note that is not guaranteed that we only
                # get the value 2. We could also receive 0's that were kept in the MQTT client
                # buffer while the connection was down but before FWE detected it.
                while self.context.received_data:
                    data = self.context.received_data.pop(0)
                    received_data_after_link_up.append(data)
                    for signal in data.get_signal_values_with_timestamps("Main_Light_Switch"):
                        value_counter[signal.value] += 1

                assert value_counter.get(2.0, 0) > 10, (
                    "Data received after link up doesn't contain value 2:"
                    f" {received_data_after_link_up}"
                )

        assert (
            1.0 not in value_counter
        ), "Value 1 should never be sent as it was set when FWE already detected the disconnection"

        # Make sure FWE is still listening to the topics after reconnecting, this is especially
        # important if persistent sessions are disabled (which is by default). When persistent
        # sessions are disabled, FWE needs to explicitly re-subscribe to the topics on reconnection.
        for attempt in Retrying():
            with attempt:
                # Sending collection scheme needs to also be retried because FWE could still receive
                # some old message containing collection schemes after the empty one is received.
                self.context.send_collection_schemes([])
                self.context.verify_checkin_documents(
                    {self.context.proto_factory.decoder_manifest_sync_id}
                )

    def send_decoder_manifest(self):
        i = 1
        while not self.stop:
            if (
                self.context.received_checkins
                and self.context.proto_factory.decoder_manifest_sync_id
                in self.context.received_checkins[-1].document_sync_ids
            ):
                log.info("Stopping decoder manifest thread as FWE already received it")
                break

            if i % 20 == 0:
                log.info("Send every 500ms the same decoder manifest iteration " + str(i))
            self.context.send_decoder_manifest()
            time.sleep(0.5)
            i += 1

    def send_collection_schemes(self):
        i = 1
        while not self.stop:
            if (
                self.context.received_checkins
                and "heartbeat" in self.context.received_checkins[-1].document_sync_ids
            ):
                log.info("Stopping collection scheme thread as FWE already received it")
                break

            if i % 20 == 0:
                log.info("Send every 500ms the same collection scheme iteration " + str(i))
            self.context.send_collection_schemes(self.collection_schemes)
            time.sleep(0.5)
            i += 1

    @pytest.mark.parametrize(
        "setup",
        [{"persistency_file": True}],
        ids=str,
        indirect=True,
    )
    def test_network_high_packet_loss(self):
        """Check if with 50% package loss FWE eventually still works"""
        self.context.start_cyclic_can_messages()
        self.context.connect_to_cloud()

        self.context.link_throttle(
            loss_percentage=50, delay_ms=50
        )  # 50% of packets lost 50ms additional delay
        self.context.start_fwe(wait_for_startup=False)
        self.thread_decoder_manifest = Thread(target=self.send_decoder_manifest)
        self.thread_decoder_manifest.start()
        self.thread_collection_schemes = Thread(target=self.send_collection_schemes)
        self.thread_collection_schemes.start()

        for attempt in Retrying(stop=stop_after_delay(300)):
            with attempt:
                assert len(self.context.received_data) > 0

    @pytest.mark.parametrize(
        "setup",
        [
            {
                "persistency_file": True,
                "someip_commands_enabled": True,
            }
        ],
        ids=str,
        indirect=True,
    )
    def test_persistency_network_link_down_then_restart(
        self, request: pytest.FixtureRequest, someipigen
    ):
        """Start with internet connection then disable internet connection wait time and then
        perform a FWE restart.
        Check if data collected while internet was down before reset is sent to cloud after restart
        """
        # On a HIL setup it is tricky to test commands with network namespace. We would need
        # to have both the simulator and FWE running in the same network namespace, but since they
        # run on different machines, that is not possible.
        check_command_persistency = not self.context.is_hil

        self.context.start_cyclic_can_messages()
        self.context.link_throttle(delay_ms=10)
        self.context.connect_to_cloud()
        self.context.link_up()

        self.context.set_can_signal("Main_Light_Switch", 0)

        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(self.collection_schemes)
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 1

        if check_command_persistency:
            # Set the command delay high enough so that FWE will get the response while the link is
            # down
            someipigen.set_response_delay_ms_for_set("Int32", 15000)
            self.context.send_actuator_command_request(
                "command123", "Vehicle.actuator1", 123, SignalType.INT32, timedelta(seconds=30)
            )
            # Need to give some time for FWE to receive the command before the network is down
            time.sleep(5)

        fwe_log = open(self.context.fwe_logfile_path)
        request.addfinalizer(fwe_log.close)
        # Advance the read pointer so that we can poll only the recent lines
        fwe_log.read()
        self.context.link_down()
        time.sleep(3)
        self.context.set_can_signal("Main_Light_Switch", 1)

        self.wait_for_fwe_disconnection(fwe_log)

        if check_command_persistency:
            assert (
                len(self.context.received_command_responses) == 0
            ), "The command response wasn't delayed enough to be sent while the link is down"

        log.info("Stop and restart FWE")
        received_data_before_reset = len(self.context.received_data)
        self.context.stop_fwe()
        self.context.set_can_signal("Main_Light_Switch", 2)
        self.context.connect_to_cloud()
        self.context.link_up()
        time.sleep(3)
        self.context.start_fwe()

        log.info("Wait 15 seconds and check data from before reset is received")
        time.sleep(15)

        found_value_one = 0
        found_value_two = 0
        for d in self.context.received_data[received_data_before_reset:]:
            signals = d.get_signal_values_with_timestamps("Main_Light_Switch")
            if len(signals) > 0 and signals[0].value == 1:
                found_value_one += 1
            if len(signals) > 0 and signals[0].value == 2:
                found_value_two += 1
        assert found_value_one >= 1
        assert found_value_two >= 1

        if check_command_persistency:
            # Check that the command response was persisted and sent when network was restored
            # We can't expect a single response as we use at least once QoS.
            assert len(self.context.received_command_responses) >= 1
            command_response = self.context.received_command_responses[0]
            assert command_response.command_id == "command123"
            assert command_response.status == command_response_pb2.COMMAND_STATUS_SUCCEEDED
            assert someipigen.get_value("Int32") == 123

    @pytest.mark.parametrize(
        "setup",
        [{"persistency_file": False}],
        ids=str,
        indirect=True,
    )
    def test_shutdown_with_link_down(self):
        """Start with internet connection then disable it and try to kill the process.

        The process should be able to gracefully shutdown.
        """
        self.context.start_cyclic_can_messages()
        self.context.connect_to_cloud()

        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(self.collection_schemes)

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        "heartbeat",
                    }
                )

        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) > 0

        self.context.link_down()
        # Just let the normal tear down run. No errors should happen when stopping the process.

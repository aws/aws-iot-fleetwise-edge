# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import re
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import List

import pytest
from testframework.common import Retrying, assert_no_log_entries
from testframework.context import Context

log = logging.getLogger(__name__)


class TestHeartbeatCollectionScheme:
    @pytest.fixture(autouse=True, params=[{}])
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        self.context_kwargs = request.param
        self.context = Context(
            tmp_path=tmp_path, worker_number=worker_number, **self.context_kwargs
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 0.0)
        self.context.set_can_signal("Parking_Brake_State", 0)
        self.context.set_can_signal("Speed", 73.25389)  # This signal's raw type is float
        self.context.set_can_signal("Voltage", 0.43523577829)  # This signal's raw type is double
        self.context.set_obd_signal("ENGINE_SPEED", 1000.0)
        self.context.set_obd_signal("ENGINE_OIL_TEMPERATURE", 98.99)
        self.context.set_obd_signal("CONTROL_MODULE_VOLTAGE", 14.5)
        self.context.set_obd_signal("EVAP_SYSTEM_VAPOR_PRESSURE_A", -34.0)
        self.context.set_obd_signal("CUSTOM_FLOAT", 42.0983)
        self.context.set_obd_signal("CUSTOM_DOUBLE", 95.28734629)

        self.context.connect_to_cloud()

        self.log_errors_to_ignore: List[re.Pattern] = []
        self.log_warnings_to_ignore: List[re.Pattern] = []
        if "use_faketime" in self.context_kwargs:
            # Clear expected errors/warnings
            # The time jump can make the MQTT connection to be interrupted
            self.log_errors_to_ignore.extend(
                [
                    re.compile(r".*?AWS_ERROR_MQTT_TIMEOUT.*"),
                    re.compile(r".*?Failed to send data.*"),
                ]
            )
            self.log_warnings_to_ignore.extend(
                [re.compile(r".*? system time .+? corresponds to a time in the past .*")]
            )
        if "use_greengrass" in self.context_kwargs:
            self.log_errors_to_ignore.extend(
                [
                    # When FWE starts, CommonAPI initializes even when not used. When trying to find
                    # the config file, CommonAPI make a getcwd system call to get the current
                    # directory, but it hard codes the max length to 255:
                    # https://github.com/COVESA/capicxx-core-runtime/blob/0e1d97ef0264622194a42f20be1d6b4489b310b5/src/CommonAPI/Runtime.cpp#L180
                    # When running with Greengrass, FWE's working dir is inside Greengrass root dir
                    # which is inside the temporary dirs that can be very long.
                    re.compile(r".*?CAPI.*?Failed to load ini file.*"),
                ]
            )
            self.log_warnings_to_ignore.extend(
                [
                    # This warning is logged by the AWS SDK when FWE is started as a Greengrass
                    # component, probably related to the AWS credentials provided by Greengrass.
                    re.compile(
                        r".*?AwsBootstrap.*?STSAssumeRoleWithWebIdentityCredentialsProvider.*"
                    )
                ]
            )

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        # Stop canigen after FWE to prevent OBD errors in the log:
        self.context.stop_cyclic_can_messages()
        self.context.destroy_aws_resources()

        assert_no_log_entries(self.context.log_errors, self.log_errors_to_ignore)
        assert_no_log_entries(self.context.log_warnings, self.log_warnings_to_ignore)

    @pytest.mark.parametrize(
        "setup",
        [
            {"use_faketime": False, "metrics_name": "heartbeat_interval_metrics"},
            {"use_faketime": True},
            {"obd_answer_reverse_order": True},
            {"use_greengrass": True},
        ],
        indirect=True,
        ids=str,
    )
    def test_heartbeat_interval(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "heartbeat",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 1000}},
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "Speed"},
                    {"name": "Voltage"},
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "CONTROL_MODULE_VOLTAGE"},
                    {"name": "EVAP_SYSTEM_VAPOR_PRESSURE_A"},
                    {"name": "CUSTOM_FLOAT"},
                    {"name": "CUSTOM_DOUBLE"},
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        log.info("Wait for two triggers with interval 5")
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2
                signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "ENGINE_SPEED"
                )
                assert len(signals) >= 1

        log.info("first received data:" + str(self.context.received_data[0]))
        log.info("second received data:" + str(self.context.received_data[1]))

        # With faketime, the first message could have arrived before faketime changed the system
        # time. In such case the first message would have a larger timestamp.
        if not self.context.use_faketime:
            assert (
                self.context.received_data[1].receive_timestamp
                >= self.context.received_data[0].receive_timestamp
            )

            timediff = (
                self.context.received_data[1].receive_timestamp
                - self.context.received_data[0].receive_timestamp
            )

            assert timediff > 900
            assert timediff < 1100

        # check OBD arrived at least in second message:
        signals = self.context.received_data[-1].get_signal_values_with_timestamps("ENGINE_SPEED")
        assert len(signals) >= 1
        log.info("ENGINE_SPEED: " + str(signals[0].value))
        assert signals[0].value > 990.0
        assert signals[0].value < 1010.0

        signals = self.context.received_data[-1].get_signal_values_with_timestamps(
            "ENGINE_OIL_TEMPERATURE"
        )
        assert len(signals) >= 1
        log.info("ENGINE_OIL_TEMPERATURE: " + str(signals[0].value))
        assert signals[0].value > 97.0
        assert signals[0].value < 100.0

        signals = self.context.received_data[-1].get_signal_values_with_timestamps(
            "CONTROL_MODULE_VOLTAGE"
        )
        assert len(signals) >= 1
        log.info("CONTROL_MODULE_VOLTAGE: " + str(signals[0].value))
        assert signals[0].value > 13.0
        assert signals[0].value < 16.0

        signals = self.context.received_data[-1].get_signal_values_with_timestamps(
            "EVAP_SYSTEM_VAPOR_PRESSURE_A"
        )
        assert len(signals) >= 1
        log.info("EVAP_SYSTEM_VAPOR_PRESSURE_A: " + str(signals[0].value))
        assert signals[0].value == -34.0

        signals = self.context.received_data[-1].get_signal_values_with_timestamps("CUSTOM_FLOAT")
        assert len(signals) >= 1
        log.info("CUSTOM_FLOAT: " + str(signals[0].value))
        assert round(signals[0].value, 4) == 42.0983

        signals = self.context.received_data[-1].get_signal_values_with_timestamps("CUSTOM_DOUBLE")
        assert len(signals) >= 1
        log.info("CUSTOM_DOUBLE: " + str(signals[0].value))
        assert signals[0].value == 95.28734629

        # check CAN timestamp are valid epoch timestamp
        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "Throttle__Position"
        )
        assert len(signals) >= 1
        signal_timestamp = (
            signals[0].relative_timestamp + self.context.received_data[1].receive_timestamp
        )
        assert (
            abs(time.time() - signal_timestamp / 1000) < 10 * 60
        ), "system under test and test platform have more than 10 minutes time difference"

        signals = self.context.received_data[1].get_signal_values_with_timestamps("Speed")
        assert len(signals) >= 1
        # The original value is 73.25389 but since FWE can only send a value to the Cloud as a
        # double it has to cast the original float to double, which results in a slightly different
        # value.
        assert round(signals[0].value, 5) == 73.25389

        signals = self.context.received_data[1].get_signal_values_with_timestamps("Voltage")
        assert len(signals) >= 1
        assert signals[0].value == 0.43523577829

    @pytest.mark.parametrize(
        "setup",
        [{"use_faketime": True}],
        indirect=True,
        ids=str,
    )
    def test_system_time_jump_to_future_after_campaign_received(self):
        """Ensure data collection continues working if the system time jumps to the future

        When FWE receives collection schemes with a start and/or expiry time, it calculates how much
        time in the future those events should happen and wait for this time. But if the system time
        changes, for example, if it started with the wrong time and was then fixed later, we need to
        ensure that the new time is taken into account and start/expiry the campaigns based on the
        new time.
        """
        fwe_startup_time = datetime.now(timezone.utc)
        faketime_start_after = timedelta(seconds=20)
        # Make time jump to the future after some time (it should be after collection schemes are
        # received).
        self.context.start_fwe(
            faketime_start_after_seconds=int(faketime_start_after.total_seconds()),
            faketime_time_spec="+2d",
        )
        self.context.send_decoder_manifest()
        collection_schemes = [
            # This campaign starts in the past and expiries in the future, so it should be active
            # right after startup.
            {
                "campaignSyncId": "heartbeat1",
                "startTime": datetime.now(timezone.utc) - timedelta(hours=1),
                "expiryTime": datetime.now(timezone.utc) + timedelta(hours=1),
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 1000}},
                "signalsToCollect": [{"name": "TireRRPrs"}],
            },
            # This campaign starts between now and the new time after faketime kicks in. So it
            # should be active after faketime_start_after_seconds elapses.
            {
                "campaignSyncId": "heartbeat2",
                "startTime": datetime.now(timezone.utc) + timedelta(days=1),
                "expiryTime": datetime.now(timezone.utc) + timedelta(days=3),
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 1000}},
                "signalsToCollect": [{"name": "Parking_Brake_State"}],
            },
        ]
        self.context.send_collection_schemes(collection_schemes)

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {"decoder_manifest_1", "heartbeat1", "heartbeat2"}
                )

        # Wait for a few messages to ensure that only one campaign is active
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 5

        all_signal_names = set()
        for received_data in self.context.received_data:
            for signal_id in received_data.signals:
                all_signal_names.add(received_data.signal_names[signal_id])
        assert all_signal_names == {"TireRRPrs"}

        # Now wait for faketime to kick in and make the system time jump to the future.
        # This should make the first collection scheme to expire. When a collection scheme is
        # expired, FWE deletes it, so the first campaign should not be in the checkin message
        # anymore.
        # If time jumps to the past again after that, ideally FWE should start the deleted
        # collection scheme again. That currently doesn't happen because FWE deleted the scheme, but
        # this behavior is acceptable because in a real world scenario, Cloud will notice that the
        # collection scheme is missing and will try to sync.
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "heartbeat2"})

        time_faketime_should_start = fwe_startup_time + faketime_start_after
        assert datetime.now(timezone.utc) > time_faketime_should_start, (
            "Faketime could have started too early. This test needs to first verify that one"
            " campaign is started correctly before faketime starts. Only after that faketime"
            " should start, which should make the previous campaign be stopped and the other"
            " campaign be started."
        )

        self.context.received_data = []
        # Wait for a few messages to ensure that only one campaign is active
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 5

        all_signal_names = set()
        for received_data in self.context.received_data:
            for signal_id in received_data.signals:
                all_signal_names.add(received_data.signal_names[signal_id])
        assert all_signal_names == {"Parking_Brake_State"}

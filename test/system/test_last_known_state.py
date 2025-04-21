# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import datetime
import logging
import time
from collections import namedtuple
from pathlib import Path
from random import Random
from typing import List, Optional

import pytest
from tenacity import stop_after_delay
from testframework.common import Retrying
from testframework.context import Context, LastKnownStateCapturedSignal
from testframework.gen import command_response_pb2
from testframework.protofactory import (
    LastKnownStateCommandOperation,
    LastKnownStateSignalInformation,
    OnChangeUpdateStrategy,
    PeriodicUpdateStrategy,
    SignalType,
    StateTemplate,
)

log = logging.getLogger(__name__)


class TestLastKnownState:
    @pytest.fixture(autouse=True, params=[{"persistency_file": False}], ids=str)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        context_kwargs = request.param
        self.command_count = 0
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            last_known_state_enabled=True,
            **context_kwargs,
        )
        self.context.start_cyclic_can_messages()
        self.context.set_can_signal("Engine_Airflow", 20.0)
        self.context.set_can_signal("Parking_Brake_State", 1)
        self.context.set_can_signal("BPDAPS_BkPDrvApP", 525)
        self.context.set_can_signal("Speed", 73.0)  # This signal's raw type is float
        self.context.set_can_signal("Voltage", 19.0)  # This signal's raw type is double
        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        self.context.stop_cyclic_can_messages()
        self.context.destroy_aws_resources()

    def send_command(
        self,
        *,
        state_template_sync_id: str,
        operation: LastKnownStateCommandOperation,
        expected_command_status=command_response_pb2.COMMAND_STATUS_SUCCEEDED,
        deactivate_after_seconds: Optional[int] = None,
    ):
        self.command_count += 1
        command_id = f"command_{self.command_count}"
        expected_command_responses = len(self.context.received_command_responses) + 1
        self.context.send_last_known_state_command_request(
            command_id=command_id,
            state_template_sync_id=state_template_sync_id,
            operation=operation,
            deactivate_after_seconds=deactivate_after_seconds,
        )

        log.info("Waiting for the command response")
        for attempt in Retrying(stop=stop_after_delay(2.5)):
            with attempt:
                assert (
                    len(self.context.received_command_responses) == expected_command_responses
                ), "No command response arrived within the expected interval"

        command_response = self.context.received_command_responses[-1]
        assert command_response.command_id == command_id
        assert command_response.status == expected_command_status

    def test_periodic(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        # Default thread idle time is 1s. It means that one sample should be uploaded on
        # each thread wake up.
        self.context.send_state_templates(
            state_templates_to_add=[
                StateTemplate(
                    sync_id="lks1",
                    signals=[
                        LastKnownStateSignalInformation(fqn="Engine_Airflow"),
                        LastKnownStateSignalInformation(fqn="Parking_Brake_State"),
                        LastKnownStateSignalInformation(fqn="Speed"),
                    ],
                    update_strategy=PeriodicUpdateStrategy(1500),
                )
            ]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "lks1"})

        # Ensure collection doesn't start until we send the command
        time.sleep(5)
        assert len(self.context.received_last_known_state) == 0

        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.ACTIVATE,
        )

        log.info("Waiting for LastKnownState data to arrive")
        for attempt in Retrying(stop=stop_after_delay(12)):
            with attempt:
                assert (
                    len(self.context.received_last_known_state) > 4
                ), "Not enough LKS data arrived within the expected interval"

        last_known_state = self.context.received_last_known_state[0]
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
            LastKnownStateCapturedSignal(
                fqn="Parking_Brake_State",
                type_=SignalType.BOOL,
                value=True,
            ),
            LastKnownStateCapturedSignal(
                fqn="Speed",
                type_=SignalType.FLOAT32,
                value=73.0,
            ),
        ]

        self.context.received_last_known_state.clear()

        # Verify that new state templates reach FWE and update time is changing.
        # Now collection frequency is higher than thread wake up
        self.context.send_state_templates(state_templates_to_remove=["lks1"])

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        # Verify that new state templates reach FWE and update time is changing.
        # Now collection frequency is higher than thread wake up
        lks2_period_ms = 5000
        lks3_period_ms = 3000
        self.context.send_state_templates(
            state_templates_to_add=[
                StateTemplate(
                    sync_id="lks2",
                    signals=[
                        LastKnownStateSignalInformation(fqn="Engine_Airflow"),
                        LastKnownStateSignalInformation(fqn="Parking_Brake_State"),
                        LastKnownStateSignalInformation(fqn="Speed"),
                    ],
                    update_strategy=PeriodicUpdateStrategy(lks2_period_ms),
                ),
                StateTemplate(
                    sync_id="lks3",
                    signals=[
                        LastKnownStateSignalInformation(fqn="BPDAPS_BkPDrvApP"),
                        LastKnownStateSignalInformation(fqn="Voltage"),
                    ],
                    update_strategy=PeriodicUpdateStrategy(lks3_period_ms),
                ),
            ]
        )

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "lks2", "lks3"})

        # Ensure collection doesn't start until we send the command
        time.sleep(max(lks2_period_ms, lks3_period_ms) / 1000 + 1)
        assert len(self.context.received_last_known_state) == 0

        # Activate the state templates and wait for the first messages, which should be the
        # snapshots
        lks2_sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        self.send_command(
            state_template_sync_id="lks2",
            operation=LastKnownStateCommandOperation.ACTIVATE,
        )
        lks3_sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        self.send_command(
            state_template_sync_id="lks3",
            operation=LastKnownStateCommandOperation.ACTIVATE,
        )
        for attempt in Retrying(stop=stop_after_delay(3)):
            with attempt:
                assert (
                    len(self.context.received_last_known_state) >= 2
                ), "Didn't receive initial snapshot within the expected interval"

        log.info("Waiting for LastKnownState data to arrive")
        for attempt in Retrying(stop=stop_after_delay(12)):
            with attempt:
                # A single message can contain data from more than one state template, so simply
                # waiting for the total number of received messages doesn't work, we have to count
                # the number of received messages per state template.
                received_last_known_state = self.context.received_last_known_state
                lks2_received_data = [
                    received
                    for received in received_last_known_state
                    if "lks2" in received.captured_signals_by_state_template
                ]
                lks3_received_data = [
                    received
                    for received in received_last_known_state
                    if "lks3" in received.captured_signals_by_state_template
                ]

                assert len(lks2_received_data) >= 3  # snapshot + 2 periods
                assert len(lks3_received_data) >= 4  # snapshot + 3 periods

        # Check snapshot
        last_known_state = lks2_received_data.pop(0)
        assert last_known_state.collection_event_time_ms_epoch >= lks2_sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks2" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks2"] == [
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
            LastKnownStateCapturedSignal(
                fqn="Parking_Brake_State",
                type_=SignalType.BOOL,
                value=True,
            ),
            LastKnownStateCapturedSignal(
                fqn="Speed",
                type_=SignalType.FLOAT32,
                value=73.0,
            ),
        ]

        last_known_state = lks2_received_data.pop(0)
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks2" in last_known_state.captured_signals_by_state_template
        assert last_known_state.collection_event_time_ms_epoch >= (
            lks2_sent_time_ms + lks2_period_ms
        )
        assert last_known_state.collection_event_time_ms_epoch <= (
            lks2_sent_time_ms + lks2_period_ms * 2
        )
        assert last_known_state.captured_signals_by_state_template["lks2"] == [
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
            LastKnownStateCapturedSignal(
                fqn="Parking_Brake_State",
                type_=SignalType.BOOL,
                value=True,
            ),
            LastKnownStateCapturedSignal(
                fqn="Speed",
                type_=SignalType.FLOAT32,
                value=73.0,
            ),
        ]
        previous_sample_sent_time = last_known_state.collection_event_time_ms_epoch
        last_known_state = lks2_received_data.pop(0)
        assert "lks2" in last_known_state.captured_signals_by_state_template
        # Verify that there are at least 5s between reported signals
        assert last_known_state.collection_event_time_ms_epoch >= (
            previous_sample_sent_time + lks2_period_ms
        )
        assert last_known_state.collection_event_time_ms_epoch <= (
            previous_sample_sent_time + lks2_period_ms * 2
        )
        assert last_known_state.captured_signals_by_state_template["lks2"] == [
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
            LastKnownStateCapturedSignal(
                fqn="Parking_Brake_State",
                type_=SignalType.BOOL,
                value=True,
            ),
            LastKnownStateCapturedSignal(
                fqn="Speed",
                type_=SignalType.FLOAT32,
                value=73.0,
            ),
        ]

        # Check snapshot
        last_known_state = lks3_received_data.pop(0)
        assert last_known_state.collection_event_time_ms_epoch >= lks3_sent_time_ms
        assert "lks3" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks3"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Voltage",
                type_=SignalType.FLOAT64,
                value=19.0,
            ),
        ]

        last_known_state = lks3_received_data.pop(0)
        assert "lks3" in last_known_state.captured_signals_by_state_template
        assert last_known_state.collection_event_time_ms_epoch >= (
            lks3_sent_time_ms + lks3_period_ms
        )
        assert last_known_state.collection_event_time_ms_epoch <= (
            lks3_sent_time_ms + lks3_period_ms * 2
        )
        assert last_known_state.captured_signals_by_state_template["lks3"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Voltage",
                type_=SignalType.FLOAT64,
                value=19.0,
            ),
        ]

        previous_sample_sent_time = last_known_state.collection_event_time_ms_epoch
        last_known_state = lks3_received_data.pop(0)
        assert "lks3" in last_known_state.captured_signals_by_state_template
        # Verify that there are at least 3s between reported signals
        assert last_known_state.collection_event_time_ms_epoch >= (
            previous_sample_sent_time + lks3_period_ms
        )
        assert last_known_state.collection_event_time_ms_epoch <= (
            previous_sample_sent_time + lks3_period_ms * 2
        )
        assert last_known_state.captured_signals_by_state_template["lks3"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Voltage",
                type_=SignalType.FLOAT64,
                value=19.0,
            ),
        ]

        previous_sample_sent_time = last_known_state.collection_event_time_ms_epoch
        last_known_state = lks3_received_data.pop(0)
        assert "lks3" in last_known_state.captured_signals_by_state_template
        # Verify that there are at least 3s between reported signals
        assert last_known_state.collection_event_time_ms_epoch >= (
            previous_sample_sent_time + lks3_period_ms
        )
        assert last_known_state.collection_event_time_ms_epoch <= (
            previous_sample_sent_time + lks3_period_ms * 2
        )
        assert last_known_state.captured_signals_by_state_template["lks3"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Voltage",
                type_=SignalType.FLOAT64,
                value=19.0,
            ),
        ]

        # Deactivate the current state templates and ensure data stops being collected.
        self.send_command(
            state_template_sync_id="lks2",
            operation=LastKnownStateCommandOperation.DEACTIVATE,
        )
        self.send_command(
            state_template_sync_id="lks3",
            operation=LastKnownStateCommandOperation.DEACTIVATE,
        )
        self.context.received_last_known_state.clear()
        time.sleep(5)
        assert len(self.context.received_last_known_state) == 0

    def test_on_change(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        self.context.send_state_templates(
            state_templates_to_add=[
                StateTemplate(
                    sync_id="lks1",
                    signals=[
                        LastKnownStateSignalInformation(fqn="Engine_Airflow"),
                        LastKnownStateSignalInformation(fqn="BPDAPS_BkPDrvApP"),
                    ],
                    update_strategy=OnChangeUpdateStrategy(),
                )
            ]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "lks1"})

        # Ensure collection doesn't start until we send the command
        time.sleep(5)
        assert len(self.context.received_last_known_state) == 0

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        deactivate_after_seconds = 8
        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.ACTIVATE,
            deactivate_after_seconds=deactivate_after_seconds,
        )
        auto_deactivate_timestamp_seconds = (
            datetime.datetime.utcnow().timestamp() + deactivate_after_seconds
        )

        # When it is activated, we should receive a snapshot containing all signals, regardless of
        # whether they changed.
        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "After an Activate command, a snapshot should be sent"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
        ]

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        log.info("Changing signal Engine_Airflow")
        self.context.set_can_signal("Engine_Airflow", 30.0)

        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "Not enough LKS data arrived within the expected interval"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=30.0,
            )
        ]

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        log.info("Changing signal BPDAPS_BkPDrvApP")
        self.context.set_can_signal("BPDAPS_BkPDrvApP", 825)

        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "Not enough LKS data arrived within the expected interval"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=825,
            ),
        ]

        if auto_deactivate_timestamp_seconds > datetime.datetime.utcnow().timestamp():
            time_to_sleep = (
                auto_deactivate_timestamp_seconds - datetime.datetime.utcnow().timestamp()
            )
            log.info(f"Waiting for {time_to_sleep} seconds until the auto-deactivate time")
            time.sleep(time_to_sleep)

        self.context.set_can_signal("Engine_Airflow", 32.0)
        self.context.set_can_signal("BPDAPS_BkPDrvApP", 225.0)

        # Ensure the signals are not collected after the auto-deactivate time passes
        time.sleep(5)
        assert len(self.context.received_last_known_state) == 0

    @pytest.mark.parametrize(
        "setup",
        [{"persistency_file": True}],
        ids=str,
        indirect=True,
    )
    def test_persisted_activation_after_restart(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        self.context.send_state_templates(
            state_templates_to_add=[
                StateTemplate(
                    sync_id="lks1",
                    signals=[
                        LastKnownStateSignalInformation(fqn="Engine_Airflow"),
                        LastKnownStateSignalInformation(fqn="BPDAPS_BkPDrvApP"),
                    ],
                    update_strategy=OnChangeUpdateStrategy(),
                )
            ]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "lks1"})

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.ACTIVATE,
        )

        # When it is activated, we should receive a snapshot containing all signals, regardless of
        # whether they changed.
        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "After an Activate command, a snapshot should be sent"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
        ]

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        log.info("Changing signal Engine_Airflow")
        self.context.set_can_signal("Engine_Airflow", 30.0)

        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "Not enough LKS data arrived within the expected interval"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=30.0,
            )
        ]

        # Restart FWE while the state template is activated. Once started again, FWE should set the
        # state template as activated without us having to send a command again.
        self.context.stop_fwe()
        self.context.connect_to_cloud()
        self.context.start_fwe()

        # Now trigger the state template with the other signal
        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        log.info("Changing signal BPDAPS_BkPDrvApP")
        self.context.set_can_signal("BPDAPS_BkPDrvApP", 825)

        # On startup, since this is an ON_CHANGE template, a message with all signals will be sent
        # as soon as first signal values arrive. Ideally, FWE shouldn't publish the data unless
        # the signal value is different from the last one that was sent. But that would require the
        # signal values to also be persisted.
        # But additionally, FWE may fail to send this initial message if it takes long for the MQTT
        # connection to be established. And since we use QoS 0 and don't persist LKS data, the data
        # won't be re-sent.
        #
        # Since we just want to ensure the state template is activated, we ignore any other message
        # that is not triggered by the signal value change.
        received_signals_after_startup: List[LastKnownStateCapturedSignal] = []
        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) > 0
                ), f"Haven't received the expected signals {received_signals_after_startup=}"
                captured_signals_by_state_template = (
                    self.context.received_last_known_state.pop().captured_signals_by_state_template
                )
                assert len(captured_signals_by_state_template) == 1
                assert "lks1" in captured_signals_by_state_template
                received_signals_after_startup.extend(captured_signals_by_state_template["lks1"])
                assert (
                    LastKnownStateCapturedSignal(
                        fqn="BPDAPS_BkPDrvApP",
                        type_=SignalType.UINT16,
                        value=825,
                    )
                    in received_signals_after_startup
                )

        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.DEACTIVATE,
        )

        self.context.set_can_signal("Engine_Airflow", 32.0)
        self.context.set_can_signal("BPDAPS_BkPDrvApP", 225.0)

        # Ensure the signals are not collected after deactivating the state template
        time.sleep(5)
        assert len(self.context.received_last_known_state) == 0

    def test_fetch_snapshot(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        self.context.send_state_templates(
            state_templates_to_add=[
                StateTemplate(
                    sync_id="lks1",
                    signals=[
                        LastKnownStateSignalInformation(fqn="Engine_Airflow"),
                        LastKnownStateSignalInformation(fqn="BPDAPS_BkPDrvApP"),
                    ],
                    update_strategy=OnChangeUpdateStrategy(),
                )
            ]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "lks1"})

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.FETCH_SNAPSHOT,
        )

        # The FetchSnapshot operation is independent from state template status, so a snapshot
        # should be sent even if the corresponding state template is deactivated.
        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "A snapshot should be sent even if state template is deactivated"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
        ]

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.ACTIVATE,
        )

        # When it is activated, we should also receive a snapshot
        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "After an Activate command, a snapshot should be sent"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
        ]

        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        log.info("Changing signal Engine_Airflow")
        self.context.set_can_signal("Engine_Airflow", 30.0)

        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "Not enough LKS data arrived within the expected interval"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=30.0,
            )
        ]

        # Request a snapshot when the state template is already activated
        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.FETCH_SNAPSHOT,
        )

        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "A snapshot should also be sent the state template is already activated"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=30.0,
            ),
        ]

        # Now deactivate the state template and request another snapshot to ensure it is still
        # sent when deactivated.
        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.DEACTIVATE,
        )
        sent_time_ms = int(datetime.datetime.utcnow().timestamp() * 1000)
        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.FETCH_SNAPSHOT,
        )

        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "A snapshot should be sent even if state template is deactivated"

        last_known_state = self.context.received_last_known_state.pop()
        assert last_known_state.collection_event_time_ms_epoch >= sent_time_ms
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=30.0,
            ),
        ]

    def test_state_template_messages_with_random_order(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1"})

        state_template_1 = StateTemplate(
            sync_id="lks1",
            signals=[
                LastKnownStateSignalInformation(fqn="Engine_Airflow"),
                LastKnownStateSignalInformation(fqn="BPDAPS_BkPDrvApP"),
            ],
            update_strategy=OnChangeUpdateStrategy(),
        )
        state_templates = [
            StateTemplate(
                sync_id="lks2",
                signals=[LastKnownStateSignalInformation(fqn="High_Beam")],
                update_strategy=PeriodicUpdateStrategy(period_ms=500),
            ),
            StateTemplate(
                sync_id="lks3",
                signals=[LastKnownStateSignalInformation(fqn="Electric_Park_Brake_Switch")],
                update_strategy=PeriodicUpdateStrategy(period_ms=500),
            ),
            StateTemplate(
                sync_id="lks4",
                signals=[LastKnownStateSignalInformation(fqn="TireRFPrs")],
                update_strategy=OnChangeUpdateStrategy(),
            ),
            StateTemplate(
                sync_id="lks5",
                signals=[LastKnownStateSignalInformation(fqn="Main_Light_Switch")],
                update_strategy=OnChangeUpdateStrategy(),
            ),
            StateTemplate(
                sync_id="lks6",
                signals=[LastKnownStateSignalInformation(fqn="TireLRPrs")],
                update_strategy=OnChangeUpdateStrategy(),
            ),
        ]

        random = Random()
        # Use a different seed every time to try to catch more issues, but log the seed so that a
        # test run can still be reproduced.
        seed = random.randint(0, 1000000)
        log.info(f"Using seed {seed} for randomizing the state template messages")
        random.seed(seed)

        MessageToSend = namedtuple(
            "MessageToSend",
            ["version", "state_templates_to_add", "state_templates_to_remove"],
        )
        messages_to_send: List[MessageToSend] = []
        for version in range(100, 121):
            # Randomly choose some state templates to be added, and from the remaining choose some
            # to be removed
            state_templates_to_choose = state_templates.copy()
            state_templates_to_add = []
            for _i in range(3):
                chosen = random.choice(state_templates_to_choose)
                state_templates_to_add.append(chosen)
                state_templates_to_choose.remove(chosen)
            state_template_to_remove = random.choice(state_templates_to_choose)
            messages_to_send.append(
                MessageToSend(version, state_templates_to_add, [state_template_to_remove.sync_id])
            )

        # This message is the one with largest version, so it is the one that should prevail at the
        # end.
        messages_to_send.append(
            MessageToSend(
                version + 1,
                [state_template_1],
                [state_template.sync_id for state_template in state_templates],
            )
        )

        # It doesn't matter which order we send the messages. Once the message with larger version
        # arrives, FWE should reject any other state template changes.
        random.shuffle(messages_to_send)
        for version, state_templates_to_add, state_templates_to_remove in messages_to_send:
            self.context.send_state_templates(
                version=version,
                state_templates_to_add=state_templates_to_add,
                state_templates_to_remove=state_templates_to_remove,
            )
            time.sleep(random.randint(0, 1000) / 1000)

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "lks1"})

        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.ACTIVATE,
        )

        # When it is activated, we should receive a snapshot containing all signals, regardless of
        # whether they changed.
        for attempt in Retrying():
            with attempt:
                assert (
                    len(self.context.received_last_known_state) == 1
                ), "After an Activate command, a snapshot should be sent"

        last_known_state = self.context.received_last_known_state.pop()
        assert len(last_known_state.captured_signals_by_state_template) == 1
        assert "lks1" in last_known_state.captured_signals_by_state_template
        assert last_known_state.captured_signals_by_state_template["lks1"] == [
            LastKnownStateCapturedSignal(
                fqn="BPDAPS_BkPDrvApP",
                type_=SignalType.UINT16,
                value=525,
            ),
            LastKnownStateCapturedSignal(
                fqn="Engine_Airflow",
                type_=SignalType.FLOAT64,
                value=20.0,
            ),
        ]

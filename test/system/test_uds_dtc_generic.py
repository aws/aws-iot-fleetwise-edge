# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import logging
import time
from pathlib import Path

import jsonschema
import pytest
from tenacity import stop_after_delay
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestUDSDTCGeneric:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            persistency_file=True,
            use_uds_dtc_generic_collection=True,
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        # We intentially set fetch condition as false
        self.context.set_dtc("ECM_DTC3", 0xAF)
        self.context.set_dtc("TCU_DTC3", 0xAF)
        self.context.set_can_signal("Throttle__Position", 0)
        self.context.set_can_signal("Vehicle_Odometer", 30000)
        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        # Stop canigen after FWE to prevent OBD errors in the log:
        self.context.stop_cyclic_can_messages()
        self.context.destroy_aws_resources()

        log.info("Expect 0 warnings and errors")
        assert self.context.log_errors == []
        assert self.context.log_warnings == []

    def load_and_validate_dtc_json(self, val):
        dtc_info = json.loads(val)
        with open(self.context.script_path + "../../../interfaces/uds-dtc/udsDtcSchema.json") as fp:
            dtc_schema = json.load(fp)
        jsonschema.validate(dtc_info, schema=dtc_schema)
        return dtc_info

    def test_time_based_fetch_dtc(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "heartbeat",
                "diagnosticsMode": "SEND_ACTIVE_DTCS",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "conditionLanguageVersion": 1,
                        "expression": "!isNull(Vehicle.ECU1.DTC_INFO)",
                        "minimumTriggerIntervalMs": 1000,
                        "triggerMode": "ALWAYS",
                    },
                },
                "signalsToCollect": [
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                    {"name": "Vehicle_Odometer"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "timeBased": {
                                "executionFrequencyMs": 1000,
                            }
                        },
                        "actions": ['custom_function("DTC_QUERY", -1, 4, -1)'],
                    }
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        "heartbeat",
                    }
                )

        log.info("Wait until DTC info received for the two failed DTCs")
        expected_data = {"AAA123": "AAA123AF0101CCDD02", "CCC789": ""}
        expected_ecu = {"AAA123": "01", "CCC789": "02"}
        for attempt in Retrying(stop=stop_after_delay(60)):
            with attempt:
                assert len(self.context.received_data) >= 1
                dtc_signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle.ECU1.DTC_INFO"
                )
                assert len(dtc_signals) > 0
        log.info(f"received dtc signals are: {dtc_signals}")

        for attempt in Retrying(stop=stop_after_delay(90)):
            with attempt:
                assert len(self.context.received_data) >= 1
                can_signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle_Odometer"
                )
                assert len(can_signals) > 0
        log.info(f"received can signals are: {can_signals}")

        assert can_signals[-1].value == 30000.0
        dtc_info = self.load_and_validate_dtc_json(dtc_signals[0].value)
        for dtc in dtc_info["DetectedDTCs"]:
            dtc_data = dtc["DTCAndSnapshot"]
            assert dtc_data["DTCStatusAvailabilityMask"] == "FF"
            for dtc_code in dtc_data["dtcCodes"]:
                assert expected_data[dtc_code["DTC"]] == dtc_code["DTCSnapshotRecord"]
                assert expected_ecu[dtc_code["DTC"]] == dtc["ECUID"]

    def test_conditional_fetch_dtc(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        # campaign with conditional fetching if throttle position is greater than 10
        collection_schemes = [
            {
                "campaignSyncId": "heartbeat",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 10000}},
                "signalsToCollect": [
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                    {"name": "Vehicle_Odometer"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "conditionBased": {
                                "conditionExpression": "Throttle__Position > 0",
                                "triggerMode": "ALWAYS",
                            }
                        },
                        "actions": ['custom_function("DTC_QUERY", -1, 4, -1)'],
                    }
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        "heartbeat",
                    }
                )

        log.info("Wait 20 to assure DTC is not fetched as fetch condition is not met")
        time.sleep(20)
        # ensure no DTC signal is received as fetch condition has not met
        if len(self.context.received_data) != 0:
            assert (
                len(
                    self.context.received_data[-1].get_signal_values_with_timestamps(
                        "Vehicle.ECU1.DTC_INFO"
                    )
                )
                == 0
            )
        # Set throttle position to 10 so that FWE will start fetching DTC
        self.context.set_can_signal("Throttle__Position", 10)
        time.sleep(5)
        self.context.set_can_signal("Throttle__Position", 0)

        log.info("Wait until DTC info received for the two failed DTCs")
        expected_data = {"AAA123": "AAA123AF0101CCDD02", "CCC789": ""}
        expected_ecu = {"AAA123": "01", "CCC789": "02"}
        for attempt in Retrying(stop=stop_after_delay(90)):
            with attempt:
                assert len(self.context.received_data) >= 1
                dtc_signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle.ECU1.DTC_INFO"
                )
                assert len(dtc_signals) > 0
        log.info("received dtc signals are " + str(dtc_signals))

        for attempt in Retrying(stop=stop_after_delay(90)):
            with attempt:
                assert len(self.context.received_data) >= 1
                can_signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle_Odometer"
                )
                assert len(can_signals) > 0
        log.info("received can signals are " + str(can_signals))

        assert can_signals[-1].value == 30000.0
        received_dtc_data = {}
        for signal in dtc_signals:
            dtc_info = self.load_and_validate_dtc_json(signal.value)
            for dtc in dtc_info["DetectedDTCs"]:
                dtc_data = dtc["DTCAndSnapshot"]
                assert dtc_data["DTCStatusAvailabilityMask"] == "FF"
                for dtc_code in dtc_data["dtcCodes"]:
                    received_dtc_data[dtc_code["DTC"]] = dtc_code["DTCSnapshotRecord"]
                    assert expected_ecu[dtc_code["DTC"]] == dtc["ECUID"]
        assert received_dtc_data == expected_data

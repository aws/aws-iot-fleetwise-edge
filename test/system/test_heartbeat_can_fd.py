# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestCanFD:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(tmp_path=tmp_path, worker_number=worker_number, use_fd=True)
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Aftertreatment1TripFuelUsed", 342)
        self.context.set_can_signal("TripTimeInDerateByEng", 12)
        self.context.set_can_signal("Aftrtrtmnt1TrpNmbrOfActvRgnrtnMn", 900)
        self.context.set_can_signal("Aftrtrtmnt2AvrgTmBtwnActvRgnrtns", 40)
        self.context.set_can_signal("Aftertreatment1TripFuelUsed", 200)

        # Using the full 64 bytes of a message
        self.context.set_can_signal("TripTimeInDerateByEngHigherBits4", 1202)
        self.context.set_can_signal("TripTimeInDerateByEngHigherBits3", 34)
        self.context.set_can_signal("TripTimeInDerateByEngHigherBits2", 234)
        self.context.set_can_signal("TripTimeInDerateByEngHigherBits1", 190)
        self.context.set_can_signal("TripTimeInDerateByEngMediumBits5", 320)
        self.context.set_can_signal("TripTimeInDerateByEngMediumBits4", 120)
        self.context.set_can_signal("TripTimeInDerateByEngMediumBits3", 231)
        self.context.set_can_signal("TripTimeInDerateByEngMediumBits2", 4)
        self.context.set_can_signal("TripTimeInDerateByEngMediumBits1", 44223)
        self.context.set_can_signal("TripTimeInDerateByEng", 156)
        self.context.set_can_signal("TripTimeInGearDown", 87)
        self.context.set_can_signal("TripTimeInTopGear", 543)
        self.context.set_can_signal("TripTimeInVSL", 90)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_can_fd(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "heartbeat",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 1000}},
                "signalsToCollect": [
                    {"name": "TripTimeInDerateByEng"},
                    {"name": "Aftrtrtmnt2AvrgTmBtwnActvRgnrtns"},
                    {"name": "Aftrtrtmnt1TrpNmbrOfActvRgnrtnMn"},
                    {"name": "TripTimeInDerateByEngHigherBits4"},
                    {"name": "TripTimeInDerateByEngHigherBits3"},
                    {"name": "TripTimeInDerateByEngHigherBits2"},
                    {"name": "TripTimeInDerateByEngHigherBits1"},
                    {"name": "TripTimeInDerateByEngMediumBits5"},
                    {"name": "TripTimeInDerateByEngMediumBits4"},
                    {"name": "TripTimeInDerateByEngMediumBits3"},
                    {"name": "TripTimeInDerateByEngMediumBits2"},
                    {"name": "TripTimeInDerateByEngMediumBits1"},
                    {"name": "TripTimeInDerateByEng"},
                    {"name": "TripTimeInGearDown"},
                    {"name": "TripTimeInTopGear"},
                    {"name": "TripTimeInVSL"},
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        log.info("Wait for two triggers")
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2

        log.info("first received data:" + str(self.context.received_data[0]))
        log.info("second received data:" + str(self.context.received_data[1]))

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

        # Make sure all received signals have correct values
        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "Aftrtrtmnt2AvrgTmBtwnActvRgnrtns"
        )
        assert signals[0].value >= 40

        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "TripTimeInDerateByEngHigherBits4"
        )
        assert signals[0].value >= 1202

        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "TripTimeInDerateByEngHigherBits3"
        )
        assert signals[0].value >= 34

        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "TripTimeInDerateByEngHigherBits2"
        )
        assert signals[0].value >= 234

        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "TripTimeInDerateByEngMediumBits1"
        )
        assert signals[0].value >= 44223

        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "TripTimeInTopGear"
        )
        assert signals[0].value >= 543

        signals = self.context.received_data[1].get_signal_values_with_timestamps("TripTimeInVSL")
        assert signals[0].value >= 90

# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestCorruptedProtoIngestion:
    """This is a Security test that validate FWE could properly reject corrupted protocol buffer
    binary received from MQTT
    """

    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
        )
        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.destroy_aws_resources()
        self.context.stop_fwe()

    def test_corrupted_proto(self):
        """ "Runs FWE and monitors the checkins sent by FWE.

        The breakdown of this test is as follows:

        1. A simulated FWE with a short checkin period is created
        2. A default decoder manifest is sent to the simulated FWE
        3. A corrupted CollectionSchemes list is sent to the FWE, we shall expect FWE to
           reject the protobuf
        4. Verify FWE is still running by inspecting its checkin message.
        """
        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        self.context.send_decoder_manifest()

        # Send a corrupted collectionScheme list
        log.info("Sending a corrupted collection scheme to Edge")
        collection_schemes = [
            {
                "campaignSyncId": "heartbeat1",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 5000}},
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "ENGINE_SPEED"},
                ],
            },
            {
                "campaignSyncId": "heartbeat2",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 1000}},
                "signalsToCollect": [
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "CONTROL_MODULE_VOLTAGE"},
                ],
            },
        ]
        self.context.send_collection_schemes(collection_schemes, corrupted=True)
        time.sleep(5)
        # Verify 1) FWE is still running; 2) The collection scheme got rejected by FWE
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                    }
                )
        time.sleep(5)
        self.context.verify_checkin_documents(
            {
                self.context.proto_factory.decoder_manifest_sync_id,
            }
        )

        # Verify the timing
        log.info("Verifying timing of checkin periods")
        self.context.verify_checkin_timing()

# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context
from testframework.gen import collection_schemes_incorrect_format_pb2

log = logging.getLogger(__name__)


class TestIncorrectFormatCollectionSchemeIngestion:
    """This is a Security test that sends incorrect format collectionSchemes to FWE and ensures
    that FWE will reject this collection scheme"""

    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(tmp_path=tmp_path, worker_number=worker_number)
        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.destroy_aws_resources()
        self.context.stop_fwe()

    def create_incorrect_collection_schemes_proto(self, collection_schemes_config):
        self.collection_schemes_proto = collection_schemes_incorrect_format_pb2.CollectionSchemes()
        self.collection_schemes_proto.timestamp_ms_epoch = round(time.time() * 1000)
        for collection_scheme_config in collection_schemes_config:
            collection_scheme = self.collection_schemes_proto.collection_schemes.add()
            collection_scheme.campaign_sync_id = collection_scheme_config.get("campaignSyncId", 1)
            collection_scheme.decoder_manifest_sync_id = (
                self.context.proto_factory.decoder_manifest_sync_id
            )
            # ~ May 12, 2021 Late Afternoon PST
            collection_scheme.start_time_ms_epoch = 1620864213000
            # ~ Sometime in the future
            # when warp speed has been reached and this collectionScheme hasn't expired yet
            collection_scheme.expiry_time_ms_epoch = 2620864213000
            collection_scheme.after_duration_ms = collection_scheme_config.get(
                "postTriggerCollectionDuration", 0
            )
            collection_scheme.include_active_dtcs = (
                collection_scheme_config.get("diagnosticsMode", "OFF") == "SEND_ACTIVE_DTCS"
            )
            if "signalsToCollect" in collection_scheme_config:
                for sig in collection_scheme_config["signalsToCollect"]:
                    collected_signal = collection_scheme.signal_information.add()
                    collected_signal.signal_id = self.context.proto_factory.signal_name_to_id[
                        sig["name"]
                    ]
                    collected_signal.incorrect_flag = True

    def test_incorrect_format_collection_scheme(self):
        """ "Runs FWE and monitors the checkins sent by FWE.

        The breakdown of this test is as follows:
        1. A simulated FWE with a short checkin period is created
        2. A default decoder manifest is sent to the simulated FWE
        3. A CollectionSchemes with incorrect schema is sent to the FWE, we shall expect FWE
           to reject it
        4. Verify FWE is still running by inspecting its checkin message.
        """
        # CollectionSchemes which will be used by the test. note campaign_sync_id was integer
        # which is incorrect format
        collection_schemes = [
            {
                "campaignSyncId": 1,
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "ENGINE_SPEED"},
                ],
            }
        ]
        self.create_incorrect_collection_schemes_proto(collection_schemes)

        self.context.start_fwe()

        # Set the name of the decoder manifest and send it
        self.context.send_decoder_manifest()

        # Send the incorrect format collection scheme
        log.info("Sending a incorrect collection scheme to Edge")
        self.context.send_collection_schemes_proto(self.collection_schemes_proto)
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

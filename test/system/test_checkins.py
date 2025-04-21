# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


@pytest.mark.parametrize(
    "setup",
    [{"use_faketime": False}, {"use_faketime": True}],
    indirect=True,
    ids=str,
)
class TestCheckins:
    """This is a Checkin test that sends collectionSchemes to FWE and ensures that FWE sends
    back the proper checkins
    """

    @pytest.fixture(autouse=True, params=[{}])
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        self.context_kwargs = request.param
        self.context = Context(
            tmp_path=tmp_path, worker_number=worker_number, **self.context_kwargs
        )
        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.destroy_aws_resources()
        self.context.stop_fwe()

    def test_checkins(self):
        """Runs FWE and monitors the checkins sent by FWE.

        The breakdown of this test is as follows:

        1. A simulated FWE with a short checkin period is created
        2. A default decoder manifest is sent to the simulated FWE
        3. The system ensures that an empty checkin was received
        4. CollectionSchemes are sent to the cloud, and checkins are read to make sure they
           contain the correct set of documents
        """
        # CollectionSchemes which will be used by the test
        collection_scheme_1 = {
            "campaignSyncId": "heartbeat1",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 5000}},
        }

        collection_scheme_2 = {
            "campaignSyncId": "heartbeat2",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 5000}},
        }

        collection_scheme_3 = {
            "campaignSyncId": "heartbeat3",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 5000}},
        }

        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        # A checkin should have been received containing no documents
        log.info("Verifying empty checkin as no documents have yet to be sent to Edge")
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(set())

        # Send a collectionScheme without a DM
        log.info("Verifying 1 CollectionScheme")
        self.context.send_collection_schemes([collection_scheme_1])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        collection_scheme_1["campaignSyncId"],
                    }
                )
        # Send the DM and then wait for it to be received
        self.context.send_decoder_manifest()
        log.info("Verifying 1 CollectionScheme and a Decoder Manifest")
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        collection_scheme_1["campaignSyncId"],
                    }
                )

        # Send a new set of collectionScheme set
        log.info(
            "Verifying a valid set of CollectionSchemes after sending collection schemes and DM"
        )
        self.context.send_collection_schemes(
            [collection_scheme_1, collection_scheme_2, collection_scheme_3]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        collection_scheme_1["campaignSyncId"],
                        collection_scheme_2["campaignSyncId"],
                        collection_scheme_3["campaignSyncId"],
                    }
                )

        # Send a subset of the previous of collectionScheme set
        log.info("Verifying deletions after sending a subset of the previous collectionSchemes")
        self.context.send_collection_schemes([collection_scheme_2, collection_scheme_3])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        collection_scheme_2["campaignSyncId"],
                        collection_scheme_3["campaignSyncId"],
                    }
                )

        # Send an empty collectionScheme
        log.info(
            "Verifying deleting all collectionSchemes on FWE after sending an empty"
            " collectionScheme list"
        )
        self.context.send_collection_schemes([])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {self.context.proto_factory.decoder_manifest_sync_id}
                )

        # Add one collectionScheme
        log.info(
            "Verifying adding one more collectionScheme after deleting everything because we"
            " like to be thorough"
        )
        self.context.send_collection_schemes([collection_scheme_3])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        collection_scheme_3["campaignSyncId"],
                    }
                )

        # Verify the timing
        log.info("Verifying timing of checkin periods")
        self.context.verify_checkin_timing()

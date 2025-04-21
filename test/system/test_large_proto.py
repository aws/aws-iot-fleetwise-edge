# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context
from testframework.gen import decoder_manifest_pb2

log = logging.getLogger(__name__)


class TestCorruptedProtoIngestion:
    """This is a test that validate FWE could handle the maximum allowed decoder manifest that can
    be sent through MQTT
    """

    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(tmp_path=tmp_path, worker_number=worker_number)
        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.destroy_aws_resources()
        self.context.stop_fwe()

    # Create decoder manifest that contains 5000 signals.
    # The size of decoder manifest will be close to 128K.
    def create_large_decoder_manifest_proto(self):
        self.context.proto_factory.decoder_manifest_proto = decoder_manifest_pb2.DecoderManifest()
        self.context.proto_factory.decoder_manifest_proto.sync_id = "decoder_manifest_1"
        self.context.proto_factory.signal_name_to_id = {}
        # This will generate a 130742 byte proto, close to 128K
        for signal_id in range(5000, 52500, 10):
            can_signal = self.context.proto_factory.decoder_manifest_proto.can_signals.add()
            can_signal.signal_id = signal_id
            can_signal.interface_id = "1"
            can_signal.message_id = signal_id
            can_signal.is_big_endian = True
            can_signal.is_signed = True
            can_signal.start_bit = 0
            can_signal.offset = 0.0
            can_signal.factor = 1.0
            can_signal.length = 8
            self.context.proto_factory.signal_name_to_id["signal_" + str(signal_id)] = signal_id
            signal_id += 1

    def test_large_proto(self):
        """Runs FWE and monitors the checkins sent by FWE.

        The breakdown of this test is as follows:

        1. A simulated FWE with a short checkin period is created
        2. A default decoder manifest is sent to the simulated FWE
        3. Publish Collection Schemes and Decoder manifest that contains 5000 signals to FWE
        4. Verify FWE software accept the Collection Schemes and Decoder Manifest.
        """
        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        # A checkin should have been received containing no documents
        for attempt in Retrying():
            with attempt:
                log.info("Verifying empty checkin as no documents have yet to be sent to FWE")
                self.context.verify_checkin_documents(set())

        # Send a large decoder manifest and collection scheme list
        log.info("Sending large collection scheme list and decoder manifest to FWE")
        self.create_large_decoder_manifest_proto()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "heartbeat1",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 5000}},
                "signalsToCollect": [],
            }
        ]
        for signal_id in range(5000, 52500, 10):
            collection_schemes[0]["signalsToCollect"].append({"name": "signal_" + str(signal_id)})
        self.context.send_collection_schemes(collection_schemes)
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "heartbeat1"})

        # Verify the timing
        log.info("Verifying timing of checkin periods")
        self.context.verify_checkin_timing()

# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestDTCCollectionScheme:
    """Test the collection and upload of DTCs"""

    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(tmp_path=tmp_path, worker_number=worker_number)
        self.context.start_cyclic_can_messages()

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_heartbeat_interval_only_dtc(self):
        """A heartbeat collectionScheme that collects no signals and only DTC
        it is expected that after the DTC was removed the answer to cloud would be comply empty/not
        sent at all
        """
        # Enable a DTC
        self.context.set_dtc("ECM_DTC1", 1)

        self.context.start_fwe()

        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "dtc",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 1000}},
                "diagnosticsMode": "SEND_ACTIVE_DTCS",
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        log.info("Wait for two triggers")
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2

        log.info("received data:" + str(self.context.received_data[0]))
        log.info("received data:" + str(self.context.received_data[1]))

        assert len(self.context.received_data[0].dtcs) == 1

        # Disable DTC
        self.context.set_dtc("ECM_DTC1", 0)
        time.sleep(5)
        received_count_after_disabling_dtc = len(self.context.received_data)
        log.info(
            "With disable DTCs and no signals to collect in collectionScheme no data should be sent"
            " to cloud"
        )
        time.sleep(5)
        assert len(self.context.received_data) == received_count_after_disabling_dtc

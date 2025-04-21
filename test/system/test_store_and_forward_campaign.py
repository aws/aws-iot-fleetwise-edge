# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import itertools
import json
import logging
import os
import time
from datetime import datetime
from pathlib import Path
from typing import Callable

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestStoreAndForwardCampaign:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            persistency_file=True,
            use_store_and_forward=True,
        )
        self.context.connect_to_cloud()
        self.context.start_fwe()
        self.context.start_cyclic_can_messages()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        if hasattr(self, "jobid") and hasattr(self, "iot_client"):
            self.iot_client.delete_job(force=True, jobId=self.jobid)
        self.context.destroy_aws_resources()

    def _send_collection_schemes(self, schemes):
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(schemes)
        self._wait_for_checkin_documents(schemes)

    def _wait_for_checkin_documents(self, schemes):
        for attempt in Retrying():
            with attempt:
                expected_documents = {"decoder_manifest_1"}
                for scheme in schemes:
                    expected_documents.add(scheme["campaignSyncId"])
                self.context.verify_checkin_documents(expected_documents)

    def _wait_for_iot_job_to_succeed(self):
        for attempt in Retrying():
            with attempt:
                assert (
                    "SUCCEEDED"
                    == self.iot_client.describe_job_execution(
                        jobId=self.jobid, thingName=str(self.context.fwe_id)
                    )["execution"]["status"]
                )

    def _restart_fwe(self):
        self.context.stop_fwe()
        self.context.connect_to_cloud()
        self.context.start_fwe()

    def _assert_signal_not_received(self, signal_name, received_data=None):
        for data in self.context.received_data if received_data is None else received_data:
            assert not data.get_signal_values_with_timestamps(signal_name)

    def _wait_for_partitions_to_have_size(self, campaign_name, partition_locations, size):
        for attempt in Retrying():
            with attempt:
                for location in partition_locations:
                    assert self._get_partition_size_on_disk(campaign_name, location) >= size

    def _assert_signal_values_equals(
        self,
        signal_name,
        predicate: Callable[[float, float], bool],
        initial_prev_value=None,
        received_data=None,
    ):
        prev_value = initial_prev_value
        signal_found = False
        for data in self.context.received_data if received_data is None else received_data:
            collect_signals = data.get_signal_values_with_timestamps(signal_name)
            if collect_signals:
                signal_found = True
                assert predicate(prev_value, collect_signals[0].value)
                prev_value = collect_signals[0].value
        assert signal_found

    def _assert_signal_timestamps_are_increasing(self, signal_name, received_data=None):
        last_signal_timestamp = None
        signal_found = False
        for data in self.context.received_data if received_data is None else received_data:
            collect_signals = data.get_signal_values_with_timestamps(signal_name)
            if collect_signals:
                signal_found = True
                signal_timestamp = collect_signals[0].relative_timestamp + data.receive_timestamp
                if last_signal_timestamp is not None:
                    assert signal_timestamp >= last_signal_timestamp
                last_signal_timestamp = signal_timestamp
        assert signal_found

    def _assert_signal_timestamps_are_before_end_time(
        self, end_time, signal_name, received_data=None
    ):
        signal_found = False
        for data in self.context.received_data if received_data is None else received_data:
            collect_signals = data.get_signal_values_with_timestamps(signal_name)
            if collect_signals:
                signal_found = True
                signal_timestamp = collect_signals[0].relative_timestamp + data.receive_timestamp
                assert signal_timestamp < end_time
        assert signal_found

    def _wait_for_received_data(self, n=None, signals=None, ignore=None):
        received_data = None
        for attempt in Retrying():
            with attempt:
                received_data = sorted(
                    self.context.received_data, key=lambda d: d.receive_timestamp
                )

                # ignore previous data
                if ignore is not None:
                    received_data = received_data[len(ignore) :]

                # verify amount of received data
                if n is None:
                    assert received_data
                else:
                    assert len(received_data) >= n

                # verify expected signals are received
                if signals is not None:
                    for signal in signals:
                        signal_present = False
                        for data in received_data:
                            if data.get_signal_values_with_timestamps(signal):
                                signal_present = True
                                break
                        assert signal_present
        return received_data

    def _get_received_data(self, ignore=None):
        received_data = sorted(self.context.received_data, key=lambda d: d.receive_timestamp)

        # ignore previous data
        if ignore is not None:
            received_data = received_data[len(ignore) :]

        assert received_data
        return received_data

    def _get_partition_size_on_disk(self, campaign_name, partition_storage_location) -> int:
        """
        Get the size of all stream files for the partition.
        This does NOT include KV stores (e.g. checkpoints)
        """
        partition_dir = os.path.join(
            self.context.persistency_folder, campaign_name, partition_storage_location
        )
        cmd = (
            f"if [ -d {partition_dir} ]; then "
            f'find {partition_dir} -type f -name "*.log"'
            " | xargs -r stat --format=%s"
            ' | awk "{s+=\\$1} END {print s}"; fi'
        )
        res = self.context.subprocess_helper.target_check_output(cmd.split(), shell=True).strip()
        file_size_sum = 0 if res == "" else int(res)
        print(f"File size sum for {partition_dir}: {file_size_sum}")
        return file_size_sum

    def test_store_and_forward_campaign(self):

        campaign_name = "collect"
        campaign_sync_id = campaign_name + "#8713432"
        partition_storage_location = "abc"

        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": f"{collect_signal} > 0.0",
                    "minimumTriggerIntervalMs": 1000,
                }
            },
            "signalsToCollect": [
                {
                    "name": collect_signal,
                    "dataPartitionId": 0,
                },
            ],
            "storeAndForwardConfiguration": [
                {
                    "storageOptions": {
                        "maximumSizeInBytes": 10000,
                        "storageLocation": partition_storage_location,
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                },
            ],
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }

        self._send_collection_schemes([collect_scheme])

        time.sleep(10)
        # setup some signal value
        self.context.set_can_signal(collect_signal, 1)

        # wait until any data has been stored
        self._wait_for_partitions_to_have_size(
            campaign_name=campaign_name, partition_locations=[partition_storage_location], size=500
        )

        # make sure signal was not forwarded
        self._assert_signal_not_received(collect_signal)
        # reset signal so collection stops
        self.context.set_can_signal(collect_signal, 0)

        # restart fwe to test that data is persisted
        self._restart_fwe()

        # trigger forwarding
        self.context.set_can_signal(forward_signal, 1)
        received_data = self._wait_for_received_data(signals=[collect_signal])
        self._assert_signal_not_received(forward_signal, received_data)
        self._assert_signal_values_equals(
            signal_name=collect_signal,
            predicate=lambda prev, curr: curr == 1,
            received_data=received_data,
        )
        self._assert_signal_timestamps_are_increasing(collect_signal, received_data)

        # stop forwarding
        self.context.set_can_signal(forward_signal, 0)
        time.sleep(3)
        received_data = self._get_received_data()

        # start collecting data again
        curr_partition_size = self._get_partition_size_on_disk(
            campaign_name, partition_storage_location
        )
        self.context.set_can_signal(collect_signal, 3)
        self._wait_for_partitions_to_have_size(
            campaign_name=campaign_name,
            partition_locations=[partition_storage_location],
            size=curr_partition_size + 500,
        )

        # verify nothing else was forwarded
        assert len(self.context.received_data) == len(received_data)

        # verify that store and forward still works after reboot

        # trigger forwarding
        self.context.set_can_signal(forward_signal, 1)

        # verify we received new data
        received_data = self._wait_for_received_data(ignore=received_data)

        self._assert_signal_not_received(forward_signal, received_data)
        self._assert_signal_values_equals(
            signal_name=collect_signal,
            predicate=lambda prev, curr: curr == 3,
            received_data=received_data,
        )
        self._assert_signal_timestamps_are_increasing(collect_signal, received_data)

    def test_store_and_forward_campaign_maximum_partition_size(self):

        campaign_name = "campaign"
        campaign_sync_id = campaign_name + "#8713432"
        partition_storage_location = "abc"
        partition_max_size_bytes = 5000

        collect_signal = "BPDAPS_BkPDrvApP"
        forward_signal = "Main_Light_Switch"

        collect_interval_ms = 50

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": collect_interval_ms}},
            "signalsToCollect": [
                {
                    "name": collect_signal,
                    "dataPartitionId": 0,
                },
            ],
            "storeAndForwardConfiguration": [
                {
                    "storageOptions": {
                        "maximumSizeInBytes": partition_max_size_bytes,
                        "storageLocation": partition_storage_location,
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                },
            ],
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }

        self._send_collection_schemes([collect_scheme])

        # periodically switch signal values
        # and check that we haven't breached max partition size
        next_signal = itertools.count().__next__
        for _i in range(0, 250):
            assert (
                self._get_partition_size_on_disk(campaign_name, partition_storage_location)
                <= partition_max_size_bytes
            )
            self.context.set_can_signal(collect_signal, next_signal())
            time.sleep(collect_interval_ms / 1000)

        # trigger forwarding
        self.context.set_can_signal(forward_signal, 1)

        # wait to receive a handful of messages
        received_data = self._wait_for_received_data(5)

        # verify that data was evicted
        # we know this because the first data we receive won't be the first data we stored
        first_value = received_data[0].get_signal_values_with_timestamps(collect_signal)[0].value
        assert first_value != 0

        self._assert_signal_not_received(forward_signal, received_data)
        self._assert_signal_values_equals(
            signal_name=collect_signal,
            predicate=lambda prev, curr: prev <= curr,
            initial_prev_value=first_value,
            received_data=received_data,
        )
        self._assert_signal_timestamps_are_increasing(collect_signal, received_data)

    def test_store_and_forward_campaign_multiple_partitions(self):

        campaign_name = "collect"
        campaign_sync_id = campaign_name + "#8713432"
        partition_0_location = "throttle_data"
        partition_1_location = "rpm_data"

        collect_condition_signal = "Brake_Lights"
        collect_signal_1 = "Low_Beam"
        collect_signal_2 = "Main_Light_Switch"
        forward_signal = "Electric_Park_Brake_Switch"

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": f"{collect_condition_signal} > 0.0",
                    "minimumTriggerIntervalMs": 100,
                }
            },
            "signalsToCollect": [
                {
                    "name": collect_signal_1,
                    "dataPartitionId": 0,
                },
                {
                    "name": collect_signal_2,
                    "dataPartitionId": 1,
                },
            ],
            "storeAndForwardConfiguration": [
                {
                    "storageOptions": {
                        "maximumSizeInBytes": 10000,
                        "storageLocation": partition_0_location,
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 1"},
                },
                {
                    "storageOptions": {
                        "maximumSizeInBytes": 10000,
                        "storageLocation": partition_1_location,
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 2"},
                },
            ],
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }

        self._send_collection_schemes([collect_scheme])

        # set up some baseline signal values
        collect_signal_1_value = 1
        self.context.set_can_signal(collect_signal_1, collect_signal_1_value)
        collect_signal_2_value = 2
        self.context.set_can_signal(collect_signal_2, collect_signal_2_value)
        # do not trigger either forward condition yet
        self.context.set_can_signal(forward_signal, 0)
        time.sleep(1)

        # start collecting data
        self.context.set_can_signal(collect_condition_signal, 1)
        self._wait_for_partitions_to_have_size(
            campaign_name=campaign_name,
            partition_locations=[partition_0_location, partition_1_location],
            size=500,
        )

        # verify data was not forwarded yet
        assert len(self.context.received_data) == 0

        # trigger forwarding of first signal
        self.context.set_can_signal(forward_signal, 1)

        received_data = self._wait_for_received_data(signals=[collect_signal_1])
        # verify signal 1 was collected
        self._assert_signal_values_equals(
            signal_name=collect_signal_1,
            predicate=lambda prev, curr: curr == collect_signal_1_value,
            received_data=received_data,
        )
        # verify other signals not collected
        for signal in [forward_signal, collect_signal_2, collect_condition_signal]:
            self._assert_signal_not_received(signal, received_data)

        # trigger forwarding of second signal
        self.context.set_can_signal(forward_signal, 2)

        received_data = self._wait_for_received_data(signals=[collect_signal_2])
        # verify signal 2 was collected
        self._assert_signal_values_equals(
            signal_name=collect_signal_2,
            predicate=lambda prev, curr: curr == collect_signal_2_value,
            received_data=received_data,
        )
        # verify other signals not collected
        for signal in [forward_signal, collect_condition_signal]:
            self._assert_signal_not_received(signal, received_data)

    def test_store_and_forward_campaign_with_iot_job(self):

        self.iot_client = self.context.get_iot_client()
        thing_arn = self.iot_client.describe_thing(thingName=str(self.context.fwe_id))["thingArn"]

        campaign_name = "collect"
        campaign_sync_id = campaign_name + "#8713432"

        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": f"{collect_signal} > 0.0",
                    "minimumTriggerIntervalMs": 1000,
                }
            },
            "signalsToCollect": [
                {
                    "name": collect_signal,
                    "dataPartitionId": 0,
                },
            ],
            "storeAndForwardConfiguration": [
                {
                    "storageOptions": {
                        "maximumSizeInBytes": 10000,
                        "storageLocation": "abc",
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                },
            ],
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }

        self._send_collection_schemes([collect_scheme])

        time.sleep(10)
        # setup some signal value
        self.context.set_can_signal(collect_signal, 1)
        time.sleep(5)
        # make sure signal was stored, not forwarded
        self._assert_signal_not_received(collect_signal)
        # reset signal so collection stops
        self.context.set_can_signal(collect_signal, 0)

        self.jobid = f"AutoTestedFwe_job_{self.context.random}"

        # Create a dictionary with the job document
        job_document = {
            "version": "1.0",
            "parameters": {"campaignArn": campaign_name},
        }

        job_document_json = json.dumps(job_document)
        # trigger forwarding
        self.iot_client.create_job(
            jobId=self.jobid,
            targets=[str(thing_arn)],
            targetSelection="SNAPSHOT",
            document=job_document_json,
            description="Data upload request for Store and Forward campaign",
        )

        received_data = self._wait_for_received_data()

        self._assert_signal_not_received(forward_signal, received_data)
        self._assert_signal_values_equals(
            signal_name=collect_signal,
            predicate=lambda prev, curr: curr == 1,
            received_data=received_data,
        )
        self._assert_signal_timestamps_are_increasing(collect_signal, received_data)

        # check to see if the job succeeded
        self._wait_for_iot_job_to_succeed()

    def test_store_and_forward_campaign_with_iot_job_end_time(self):

        self.iot_client = self.context.get_iot_client()
        thing_arn = self.iot_client.describe_thing(thingName=str(self.context.fwe_id))["thingArn"]

        campaign_name = "collect"
        campaign_sync_id = campaign_name + "#8713432"

        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": f"{collect_signal} > 0.0",
                    "minimumTriggerIntervalMs": 1000,
                }
            },
            "signalsToCollect": [
                {
                    "name": collect_signal,
                    "dataPartitionId": 0,
                },
            ],
            "storeAndForwardConfiguration": [
                {
                    "storageOptions": {
                        "maximumSizeInBytes": 10000,
                        "storageLocation": "abc",
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                },
            ],
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }

        self._send_collection_schemes([collect_scheme])
        time.sleep(10)
        # setup some signal value
        self.context.set_can_signal(collect_signal, 1)
        # sleeping for 1s to align with the minTriggerInterval
        time.sleep(1)
        # make sure signal was stored, not forwarded
        self._assert_signal_not_received(collect_signal)
        # reset signal so collection stops
        self.context.set_can_signal(collect_signal, 0)
        # set endTime after we collect the first batch of data
        # Add 5s buffer to allow forwarding of data which is collected
        future_time = datetime.fromtimestamp(datetime.now().timestamp() + 5)
        end_time = future_time.replace(microsecond=0).isoformat() + "+00:00"
        # Parse the string into a datetime object
        end_time_dt = datetime.fromisoformat(end_time)
        # Convert the datetime object to milliseconds since the Unix epoch
        end_time_ms = int(end_time_dt.timestamp() * 1000)
        # setup some signal value
        self.context.set_can_signal(collect_signal, 1)
        time.sleep(5)
        # make sure signal was stored, not forwarded
        self._assert_signal_not_received(collect_signal)
        # reset signal so collection stops
        self.context.set_can_signal(collect_signal, 0)

        self.jobid = f"AutoTestedFwe_job_{self.context.random}"

        # Create a dictionary with the job document
        job_document = {
            "version": "1.0",
            "parameters": {
                "campaignArn": campaign_name,
                "endTime": end_time,
            },
        }

        job_document_json = json.dumps(job_document)

        log.info(job_document_json)

        # trigger forwarding
        self.iot_client.create_job(
            jobId=self.jobid,
            targets=[str(thing_arn)],
            targetSelection="SNAPSHOT",
            document=job_document_json,
            description="Data upload request for Store and Forward campaign",
        )

        received_data = self._wait_for_received_data()

        self._assert_signal_not_received(forward_signal, received_data)
        self._assert_signal_values_equals(
            signal_name=collect_signal,
            predicate=lambda prev, curr: curr == 1,
            received_data=received_data,
        )
        self._assert_signal_timestamps_are_increasing(collect_signal, received_data)

        self._assert_signal_timestamps_are_before_end_time(
            end_time_ms, collect_signal, received_data
        )

        # check to see if the job succeeded
        self._wait_for_iot_job_to_succeed()

    def test_store_and_forward_with_unknown_signal_in_expression(self):

        campaign_name = "unknown_signal_in_collection_condition"
        campaign_sync_id = campaign_name + "#8713432"
        partition_storage_location = "abc"

        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": f"{collect_signal} > 0.0",
                    "minimumTriggerIntervalMs": 1000,
                }
            },
            "signalsToCollect": [
                {
                    "name": collect_signal,
                    "dataPartitionId": 0,
                },
            ],
            "storeAndForwardConfiguration": [
                {
                    "storageOptions": {
                        "maximumSizeInBytes": 10000,
                        "storageLocation": partition_storage_location,
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                },
            ],
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }
        self.context.send_decoder_manifest()
        self.context.proto_factory.create_collection_schemes_proto([collect_scheme])
        condition_tree = self.context.proto_factory.collection_schemes_proto.collection_schemes[
            0
        ].condition_based_collection_scheme.condition_tree

        # Using "UNKNOWN" signal in collection condition
        condition_node = condition_tree.node_operator.left_child
        condition_node.node_signal_id = 123456

        self.context.send_collection_schemes_proto(
            self.context.proto_factory.collection_schemes_proto
        )
        self._wait_for_checkin_documents([collect_scheme])
        time.sleep(10)
        # setup some signal value
        self.context.set_can_signal(collect_signal, 1)
        self.context.set_can_signal(forward_signal, 1)

        # Wait for a reasonable amount of time to ensure no data is collected
        time.sleep(20)

        # Verify no data was collected or forwarded
        assert len(self.context.received_data) == 0
        self._assert_signal_not_received(forward_signal)

        assert self._get_partition_size_on_disk(campaign_name, partition_storage_location) == 0

    def test_store_and_forward_with_unknown_signal_in_forward_condition(self):
        campaign_name = "unknown_signal_in_forward_condition"
        campaign_sync_id = campaign_name + "#8713432"
        partition_storage_location = "abc"

        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": f"{collect_signal} > 0.0",
                    "minimumTriggerIntervalMs": 1000,
                }
            },
            "signalsToCollect": [
                {
                    "name": collect_signal,
                    "dataPartitionId": 0,
                },
            ],
            "storeAndForwardConfiguration": [
                {
                    "storageOptions": {
                        "maximumSizeInBytes": 10000,
                        "storageLocation": partition_storage_location,
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                },
            ],
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }
        self.context.send_decoder_manifest()
        self.context.proto_factory.create_collection_schemes_proto([collect_scheme])

        condition_tree = (
            self.context.proto_factory.collection_schemes_proto.collection_schemes[0]
            .store_and_forward_configuration.partition_configuration[0]
            .upload_options.condition_tree
        )
        # Using UNKNOWN signal in forward condition
        condition_node = condition_tree.node_operator.left_child
        condition_node.node_signal_id = 123456
        self.context.send_collection_schemes_proto(
            self.context.proto_factory.collection_schemes_proto
        )
        self._wait_for_checkin_documents([collect_scheme])
        time.sleep(10)

        # setup some signal value
        self.context.set_can_signal(collect_signal, 1)
        forward_signal = "SIGNAL_NOT_IN_DM"
        self.context.set_can_signal(forward_signal, 1)

        time.sleep(10)

        # Verify no data was forwarded
        assert len(self.context.received_data) == 0
        self._assert_signal_not_received(forward_signal)

        # Assert that there was data collected on the disk
        assert self._get_partition_size_on_disk(campaign_name, partition_storage_location) > 0

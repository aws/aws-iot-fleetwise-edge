# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import binascii
import copy
import json
import logging
import os
import re
import resource
import subprocess
import time
from collections import namedtuple
from concurrent.futures import Future
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from enum import Enum
from pathlib import Path
from threading import Lock, Thread
from typing import Any, Dict, List, Optional, Union
from xml.etree import ElementTree

import boto3
from amazon.ion import simpleion
from awscrt import io, mqtt5
from awsiot import mqtt5_client_builder
from boto3.session import Session
from can_command_server import CanCommandServer
from canigen import Canigen
from google.protobuf.json_format import MessageToJson
from snappy import snappy
from tenacity import stop_after_delay
from testframework.aws_thing_creator import AwsThing
from testframework.can_gateway import CanGateway
from testframework.common import Retrying, is_hil
from testframework.gen import (
    checkin_pb2,
    command_request_pb2,
    command_response_pb2,
    last_known_state_data_pb2,
    vehicle_data_pb2,
)
from testframework.gen.state_templates_pb2 import StateTemplates
from testframework.greengrass import GreengrassHelper
from testframework.network_namespace import NetworkNamespace
from testframework.process_utils import ProcessWrapper, SubprocessHelper, stop_process_and_wait
from testframework.protofactory import (
    LastKnownStateCommandOperation,
    ProtoFactory,
    SignalType,
    StateTemplate,
)
from testframework.someip import SOMEIP_SERVICE_ID_CAN, create_fwe_config

log = logging.getLogger(__name__)

ReceivedSignal = namedtuple("ReceivedSignal", "value relative_timestamp")


@dataclass(init=False)
class ReceivedCheckin:
    """This class decodes and contains checkin data from serialized checkin proto binary data"""

    timestamp_ms_epoch: int
    document_sync_ids: List[str]

    def __init__(self, checkin_proto_data):
        """Initializes member variables of this class to data contained in checkin_proto_data.
        If it failed to parse from string this will raise an exception"""
        checkin_proto = checkin_pb2.Checkin()
        checkin_proto.ParseFromString(checkin_proto_data)
        self.timestamp_ms_epoch = checkin_proto.timestamp_ms_epoch
        self.document_sync_ids = checkin_proto.document_sync_ids


class ReceivedData:
    """This class contains received vehicle data"""

    def __init__(self, payload, decoder_manifest, signal_name_to_signal_id):
        self.payload = payload
        self.signal_names = {}
        self.signal_pids = {}
        self.signal_units = {}
        self.signal_name_to_signal_id = signal_name_to_signal_id
        self.dtcs = []
        for s in decoder_manifest.can_signals:
            for name, signal_id in self.signal_name_to_signal_id.items():
                if signal_id == s.signal_id:
                    self.signal_names[s.signal_id] = name
                    break
            # Todo: after unit got deleted from decoder manifest also directly get it from DBC
            self.signal_units[s.signal_id] = "-"
        for o in decoder_manifest.obd_pid_signals:
            self.signal_pids[o.signal_id] = o.pid

        output = ""
        kep = vehicle_data_pb2.VehicleData()
        kep.ParseFromString(self.payload)
        self.campaign_sync_id = kep.campaign_sync_id
        self.receive_timestamp = kep.collection_event_time_ms_epoch
        self.event_id = kep.collection_event_id
        output += (
            "{ CollectionScheme: "
            + kep.campaign_sync_id
            + " triggered at: "
            + str(kep.collection_event_time_ms_epoch)
            + " "
        )
        self.signals = {}
        self.signals_timestamps = {}

        self.uploaded_s3_objects = []

        for s in kep.captured_signals:
            if s.signal_id not in self.signals:
                self.signals[s.signal_id] = []
                self.signals_timestamps[s.signal_id] = []
            if s.string_value != "":
                self.signals[s.signal_id].append(s.string_value)
            else:
                self.signals[s.signal_id].append(s.double_value)
            self.signals_timestamps[s.signal_id].append(s.relative_time_ms)
        if kep.dtc_data:
            for s in kep.dtc_data.active_dtc_codes:
                self.dtcs.append(s)
        for o in kep.s3_objects:
            if o.key in self.uploaded_s3_objects:
                log.error("Received duplicate uploaded S3 object key: " + o.key)
                continue
            self.uploaded_s3_objects.append(o.key)

        # assemble the output string:
        output += " Signals:{\n"
        for s in self.signals:
            if s in self.signal_names:
                output += (
                    "can signal "
                    + str(self.signal_names[s])
                    + "("
                    + str(s)
                    + ") in "
                    + str(self.signal_units[s])
                    + " [relative timestamp of first sample: "
                    + str(self.signals_timestamps[s][0])
                    + "ms, last sample:  "
                    + str(self.signals_timestamps[s][-1])
                    + "ms]: "
                )
            elif s in self.signal_pids:
                output += (
                    "obd signal for pid "
                    + str(self.signal_pids[s])
                    + "("
                    + str(s)
                    + ") [relative timestamp of first sample: "
                    + str(self.signals_timestamps[s][0])
                    + "ms, last sample:  "
                    + str(self.signals_timestamps[s][-1])
                    + "ms]: "
                )

            count = 0
            last_value = 0
            for d in self.signals[s]:
                if d == last_value:
                    count = count + 1
                else:
                    if count >= 5:
                        output += (
                            "[" + str(last_value) + "  repeated " + str(count - 5) + " times ], "
                        )
                    count = 0
                    last_value = d
                if count < 5:
                    output += str(d) + ", "
            if count >= 5:
                output += "[" + str(last_value) + "  repeated " + str(count - 5) + " times ], "
            output += "\n"
        output += "}\n Active DTCs:{\n"
        for d in self.dtcs:
            output += str(d) + ", "
        if len(self.uploaded_s3_objects) > 0:
            output += "}\n S3-Uploaded-Objects:{\n"
            output += ",".join(self.uploaded_s3_objects)

        output += "}}"
        self.output = output

    def get_signal_values_with_timestamps(self, name: str) -> List[ReceivedSignal]:
        output = []
        signal_id = -1
        if name not in self.signal_name_to_signal_id:
            return []
        else:
            signal_id = self.signal_name_to_signal_id[name]
        if signal_id not in self.signals:
            return []
        index = 0
        for s in self.signals[signal_id]:
            output.append(
                ReceivedSignal(
                    value=s, relative_timestamp=self.signals_timestamps[signal_id][index]
                )
            )
            index = index + 1
        return output

    def __str__(self):
        return self.output

    def append(self, received_data):
        for signal_id in received_data.signals:
            if signal_id not in self.signals:
                self.signals[signal_id] = []
                self.signals_timestamps[signal_id] = []
            self.signals[signal_id] += received_data.signals[signal_id]
            self.signals_timestamps[signal_id] += received_data.signals_timestamps[signal_id]


@dataclass(init=False)
class ReceivedCommandResponse:
    command_id: str
    status = command_response_pb2.Status
    reason_code: int
    reason_description: str

    def __init__(self, payload: bytes) -> None:
        command_response_proto = command_response_pb2.CommandResponse()
        command_response_proto.ParseFromString(payload)
        self.command_id = command_response_proto.command_id
        self.status = command_response_proto.status
        self.reason_code = command_response_proto.reason_code
        self.reason_description = command_response_proto.reason_description


class LastKnownStateUpdateStrategy(Enum):
    PERIODIC = 0
    ON_CHANGE = 1


@dataclass
class LastKnownStateCapturedSignal:
    fqn: str
    type_: SignalType
    value: Union[bool, int, float, str]


@dataclass(init=False)
class ReceivedLastKnownStateData:
    collection_event_time_ms_epoch: int
    captured_signals_by_state_template: Dict[str, List[LastKnownStateCapturedSignal]]

    def __init__(self, payload: bytes, signal_name_to_signal_id: Dict[str, int]) -> None:
        signal_id_to_signal_name = dict(
            zip(signal_name_to_signal_id.values(), signal_name_to_signal_id.keys())
        )
        payload = snappy.decompress(payload)
        data_proto = last_known_state_data_pb2.LastKnownStateData()
        data_proto.ParseFromString(payload)
        self.collection_event_time_ms_epoch: int = data_proto.collection_event_time_ms_epoch
        self.captured_signals_by_state_template: Dict[str, List[LastKnownStateCapturedSignal]] = {}
        for captured_state_template_signals in data_proto.captured_state_template_signals:
            self.captured_signals_by_state_template[
                captured_state_template_signals.state_template_sync_id
            ] = captured_signals = []
            for captured_signal in captured_state_template_signals.captured_signals:
                value_attr_to_signal_type = {
                    "double_value": SignalType.FLOAT64,
                    "boolean_value": SignalType.BOOL,
                    "int8_value": SignalType.INT8,
                    "uint8_value": SignalType.UINT8,
                    "int16_value": SignalType.INT16,
                    "uint16_value": SignalType.UINT16,
                    "int32_value": SignalType.INT32,
                    "uint32_value": SignalType.UINT32,
                    "int64_value": SignalType.INT64,
                    "uint64_value": SignalType.INT64,
                    "float_value": SignalType.FLOAT32,
                    "string_value": SignalType.FLOAT64,
                }
                value = None
                signal_type = None
                for value_attr, type_ in value_attr_to_signal_type.items():
                    if captured_signal.HasField(value_attr):
                        value = getattr(captured_signal, value_attr)
                        signal_type = type_
                        break

                assert value is not None, "None of the expected value fields was set"
                assert signal_type is not None

                captured_signals.append(
                    LastKnownStateCapturedSignal(
                        fqn=signal_id_to_signal_name[captured_signal.signal_id],
                        type_=signal_type,
                        value=value,
                    )
                )
                captured_signals.sort(key=lambda x: x.fqn)


@dataclass
class RawDataBufferSignalOverridesConfig:
    interface_id: str
    message_id: str
    reserved_bytes: Optional[int] = None
    max_samples: Optional[int] = None
    max_bytes_per_sample: Optional[int] = None
    max_bytes: Optional[int] = None

    def to_config_dict(self) -> Dict:
        json = {
            "interfaceId": self.interface_id,
            "messageId": self.message_id,
            "reservedSize": self.reserved_bytes,
            "maxSamples": self.max_samples,
            "maxSizePerSample": self.max_bytes_per_sample,
            "maxSize": self.max_bytes,
        }
        # If any config is None just leave it out to ensure that FWE sets its own defaults
        json = {k: v for k, v in json.items() if v is not None}

        return json


@dataclass
class RawDataBufferConfig:
    max_bytes: Optional[int] = None
    reserved_bytes_per_signal: Optional[int] = None
    max_samples_per_signal: Optional[int] = None
    max_bytes_per_sample: Optional[int] = None
    max_bytes_per_signal: Optional[int] = None
    overrides_per_signal: List[RawDataBufferSignalOverridesConfig] = field(default_factory=list)

    def to_config_dict(self) -> Dict:
        json = {
            "maxSize": self.max_bytes,
            "reservedSizePerSignal": self.reserved_bytes_per_signal,
            "maxSamplesPerSignal": self.max_samples_per_signal,
            "maxSizePerSample": self.max_bytes_per_sample,
            "maxSizePerSignal": self.max_bytes_per_signal,
        }
        # If any config is None just leave it out to ensure that FWE sets its own defaults
        json = {k: v for k, v in json.items() if v is not None}

        json["overridesPerSignal"] = [
            overrides.to_config_dict() for overrides in self.overrides_per_signal
        ]

        return json


class ComplexSignalBytes:
    """
    Wrapper for the data received from complex signals

    Since the data can be quite large, it could pollute the test output in case of failure, even
    reaching output size limit in CI jobs.
    This wrapper limits the string representation so it is log friendly.
    """

    def __init__(self, byte_values: bytes):
        self.byte_values = byte_values

    def __repr__(self) -> str:
        return str(self)

    def __str__(self) -> str:
        return f"{self.byte_values[:1000].hex()}...<TRUNCATED>..."


@dataclass
class ThreadStat:
    tid: int
    name: str
    user_time_seconds: float
    kernel_time_seconds: float
    start_time_seconds: float
    run_time_seconds: float

    def __sub__(self, other):
        return ThreadStat(
            tid=self.tid,
            name=self.name,
            user_time_seconds=self.user_time_seconds - other.user_time_seconds,
            kernel_time_seconds=self.kernel_time_seconds - other.kernel_time_seconds,
            start_time_seconds=other.start_time_seconds + other.run_time_seconds,
            run_time_seconds=self.run_time_seconds - other.run_time_seconds,
        )


@dataclass
class ProcessStat:
    pid: int
    name: str
    user_time_seconds: float
    kernel_time_seconds: float
    start_time_seconds: float
    run_time_seconds: float
    thread_stats: Dict[int, ThreadStat]

    def __sub__(self, other):
        thread_stats = {}
        for thread_stat in self.thread_stats.values():
            if thread_stat.tid in other.thread_stats:
                thread_stats[thread_stat.tid] = thread_stat - other.thread_stats[thread_stat.tid]
            else:
                thread_stats[thread_stat.tid] = thread_stat
        return ProcessStat(
            pid=self.pid,
            name=self.name,
            user_time_seconds=self.user_time_seconds - other.user_time_seconds,
            kernel_time_seconds=self.kernel_time_seconds - other.kernel_time_seconds,
            start_time_seconds=other.start_time_seconds + other.run_time_seconds,
            run_time_seconds=self.run_time_seconds - other.run_time_seconds,
            thread_stats=thread_stats,
        )


class AwsSdkLogLevel(Enum):
    Off = 0
    Fatal = 1
    Error = 2
    Warn = 3
    Info = 4
    Debug = 5
    Trace = 6


class Context:
    CHECKIN_PERIOD = 2000  # ms

    def __init__(
        self,
        tmp_path: Path,
        worker_number: int,
        enable_network_namespace=False,
        high_load=False,
        number_channels=1,
        persistency_file=False,
        use_extended_ids=False,
        use_fd=False,
        background_metrics=False,
        use_faketime=False,
        use_obd_broadcast=False,
        obd_answer_reverse_order=False,
        enable_remote_profiler=False,
        metrics_name="",  # This enables metrics upload
        ros2_enabled=False,
        ros2_config_file="ros_config.json",
        raw_data_buffer_config: Optional[RawDataBufferConfig] = None,
        keep_alive_interval_seconds: Optional[int] = None,
        ping_timeout_ms: Optional[int] = None,
        session_expiry_interval_seconds: Optional[int] = None,
        aws_sdk_log_level=AwsSdkLogLevel.Info,
        use_greengrass=False,
        use_can_to_someip=False,
        someip_commands_enabled=False,
        can_commands_enabled=False,
        use_someip_collection=False,
        use_someip_signals=False,
        use_someip_device_shadow=False,
        someipigen_instance="",
        someip_instance_id_can="",
        someip_device_shadow_editor_instance="",
        last_known_state_enabled=False,
        use_uds_dtc_generic_collection=False,
        custom_decoder_json_files=None,
        node_json_files=None,
        use_named_signal_data_source=False,
        use_store_and_forward=False,
        use_script_engine=False,
        extra_static_config=None,
    ):
        enabled_values = ["true", "1", "on", "yes"]
        self.metrics_lock = Lock()
        self.callgrind = os.environ.get("TEST_CALLGRIND", "false").lower() in enabled_values
        self.massif = os.environ.get("TEST_MASSIF", "false").lower() in enabled_values
        self.memcheck = os.environ.get("TEST_MEMCHECK", "false").lower() in enabled_values
        self.random = binascii.hexlify(os.urandom(5)).decode("utf-8")
        self.metrics_output = []
        self.metrics_not_yet_uploaded = []
        self.high_load = high_load
        self.use_fd = use_fd
        self.use_obd_broadcast = use_obd_broadcast
        self.obd_answer_reverse_order = obd_answer_reverse_order
        self.use_greengrass = use_greengrass
        self.use_can_to_someip = use_can_to_someip
        self.number_channels = number_channels
        self.metrics_name = metrics_name
        self.metrics_to_check: Dict[str, float] = {}
        self.start_time = datetime.now()
        self.receive_data_counter = 0
        self.receive_data_bytes = 0
        self.received_samples_counter = 0
        self.collected_samples_counter = 0
        self.ram_limit = 3000000
        self.fwe_process_stat_start: Optional[ProcessStat] = None
        self.fwe_process_stat_stop: Optional[ProcessStat] = None
        self.fwe_process: Optional[ProcessWrapper] = None
        self.branch = os.environ.get("TEST_BRANCH", "local-test")
        # Use thing name pattern as the allowed branch name pattern
        invalid_branch_pattern = r"[^a-zA-Z0-9:_-]"
        if re.search(invalid_branch_pattern, self.branch):
            log.warning(
                f"Branch name '{self.branch}' contains invalid characters. Replacing them with '_'"
            )
            self.branch = re.sub(invalid_branch_pattern, "_", self.branch)
        self._delete_aws_thing_at_the_end = False
        self.use_extended_ids = use_extended_ids
        self.use_faketime = use_faketime
        self.ros2_enabled = ros2_enabled
        self.worker_number = worker_number
        self.someip_commands_enabled = someip_commands_enabled
        self.can_commands_enabled = can_commands_enabled
        self.use_someip_collection = use_someip_collection
        self.use_someip_signals = use_someip_signals
        self.use_someip_device_shadow = use_someip_device_shadow
        self.last_known_state_enabled = last_known_state_enabled
        self.use_uds_dtc_generic_collection = use_uds_dtc_generic_collection
        self.use_named_signal_data_source = use_named_signal_data_source
        self.use_store_and_forward = use_store_and_forward
        self.use_script_engine = use_script_engine
        self.log_errors = []
        self.log_warnings = []
        self._incomplete_log_line = ""
        self.script_path = os.path.abspath(os.path.join(os.path.dirname(__file__))) + "/"
        self.runpath = tmp_path
        log.info("Create context with random value: " + self.random)
        self.memcheck_output = self.runpath / "fwe-memcheck.xml"
        self.is_hil = is_hil()
        self.upload_metrics = (
            os.environ.get("TEST_UPLOAD_METRICS", "false").lower() in enabled_values
        )
        self.metrics_platform = "EC2_amd64" if not self.is_hil else "S32G_arm64"
        self.branch_for_alarms = os.environ.get("TEST_CHECK_METRICS_FOR_BRANCH", "")
        required_hil_env_vars = ["BOARD_IP_ADDRESS", "BOARD_USERNAME"]
        missing_hil_env_vars = set(required_hil_env_vars).difference(os.environ.keys())
        if self.is_hil and missing_hil_env_vars:
            raise Exception(
                f"{missing_hil_env_vars} environment variables are not set for HIL mode"
            )

        self.network_namespace: Optional[NetworkNamespace] = None
        if enable_network_namespace:
            self.network_namespace = NetworkNamespace(worker_number=worker_number)

        self.subprocess_helper = SubprocessHelper(as_root_user=self.network_namespace is not None)
        self.subprocess_helper_as_current_user = SubprocessHelper(as_root_user=False)

        self.binary_path = os.path.abspath(os.environ["TEST_FWE_BINARY"])
        # Add branch and metrics name to the client id so that we can extract metrics from AWS IoT
        # vended logs.
        self.client_id = f"AutoTester_{self.branch}_{self.metrics_name}_{self.random}"

        self._delete_aws_thing_at_the_end = False
        if os.environ.get("TEST_CREATE_NEW_AWS_THING", "false").lower() in enabled_values:
            self.fwe_id = f"AutoTestedFwe_{self.branch}_{self.metrics_name}_{self.random}"
            self._aws_thing = AwsThing(
                tmp_path,
                self.fwe_id,
                s3_bucket_name=os.environ["TEST_S3_BUCKET_NAME"] if ros2_enabled else None,
                use_greengrass=use_greengrass,
            )

            self.cert = str(self._aws_thing.cert_pem_path)
            self.key = str(self._aws_thing.private_key_path)
            self.endpoint = self._aws_thing.endpoint
            self.creds_endpoint = self._aws_thing.creds_endpoint

            self._delete_aws_thing_at_the_end = True

        else:
            required_thing_env_vars = {
                "TEST_THING_NAME",
                "TEST_ENDPOINT",
                "TEST_CREDS_ENDPOINT",
                "TEST_KEY",
                "TEST_CERT",
            }
            missing_thing_env_vars = required_thing_env_vars.difference(os.environ.keys())
            if missing_thing_env_vars:
                raise Exception(
                    "TEST_CREATE_NEW_AWS_THING is false and the following variables "
                    f" are missing: {missing_thing_env_vars}"
                )
            self.fwe_id = os.environ["TEST_THING_NAME"]
            self.endpoint = os.environ["TEST_ENDPOINT"]
            self.creds_endpoint = os.environ["TEST_CREDS_ENDPOINT"]
            self.cert = os.environ["TEST_CERT"]
            self.key = os.environ["TEST_KEY"]

        # Set the MQTT Topics to a prefix plus a random number. Intentionally omit the trailing "/"
        # to reduce the number of levels. IoT Core allows only up to 8 levels in a non-reserved
        # topic, which could be violated by some command topics.
        random_prefix = f"AutomatedTest_{self.random}_"
        iotfleetwise_topic_prefix = f"{random_prefix}iotfleetwise/"
        self.can_data_topic = f"{iotfleetwise_topic_prefix}vehicles/{self.fwe_id}/signals"
        self.checkin_topic = f"{iotfleetwise_topic_prefix}vehicles/{self.fwe_id}/checkins"
        self.decoder_manifest_topic = (
            f"{iotfleetwise_topic_prefix}vehicles/{self.fwe_id}/decoder_manifests"
        )
        self.collection_scheme_list_topic = (
            f"{iotfleetwise_topic_prefix}vehicles/{self.fwe_id}/collection_schemes"
        )
        self.lifecycle_connected_topic = f"$aws/events/presence/connected/{self.fwe_id}"
        self.lifecycle_disconnected_topic = f"$aws/events/presence/disconnected/{self.fwe_id}"
        self.lifecycle_subscribed_topic = f"$aws/events/subscriptions/subscribed/{self.fwe_id}"
        self.lifecycle_unsubscribed_topic = f"$aws/events/subscriptions/unsubscribed/{self.fwe_id}"

        self.commands_topic_prefix = f"{random_prefix}commands/"

        self.command_request_topic = (
            f"{self.commands_topic_prefix}things/{self.fwe_id}/executions/+/request/protobuf"
        )
        self.command_response_topic = (
            f"{self.commands_topic_prefix}things/{self.fwe_id}/executions/+/response/protobuf"
        )

        self.last_known_state_config_topic = (
            f"{iotfleetwise_topic_prefix}vehicles/{self.fwe_id}/last_known_states/config"
        )
        self.last_known_state_data_topic = (
            f"{iotfleetwise_topic_prefix}vehicles/{self.fwe_id}/last_known_states/data"
        )

        self.received_data: List[ReceivedData] = []
        self.received_checkins = []
        self.mqtt_client: Optional[mqtt5.Client] = None
        self.stop_future: Optional[Future] = None

        self.received_command_responses: List[ReceivedCommandResponse] = []

        self.received_last_known_state: List[ReceivedLastKnownStateData] = []

        self.lifecycle_connected_events: List[Dict] = []
        self.lifecycle_disconnected_events: List[Dict] = []
        self.lifecycle_subscribed_events: List[Dict] = []
        self.lifecycle_unsubscribed_events: List[Dict] = []

        initial_config = os.getcwd() + "/../../configuration/static-config.json"
        if not Path(initial_config).is_file():
            raise Exception(
                "Initial config can not be loaded from file "
                + initial_config
                + " because file does not exist."
            )

        self.config_file = str(self.runpath / "example-config.json")

        with open(initial_config) as read_file:
            self.config_content = json.loads(read_file.read())

        self.config_content["staticConfig"]["mqttConnection"]["clientId"] = self.fwe_id
        self._greengrass_helper: Optional[GreengrassHelper] = None
        self._greengrass_log_reader_process: Optional[ProcessWrapper] = None
        if use_greengrass:
            del self.config_content["staticConfig"]["mqttConnection"]["endpointUrl"]
            del self.config_content["staticConfig"]["mqttConnection"]["certificateFilename"]
            del self.config_content["staticConfig"]["mqttConnection"]["privateKeyFilename"]
            del self.config_content["staticConfig"]["mqttConnection"]["keepAliveIntervalSeconds"]
            del self.config_content["staticConfig"]["mqttConnection"]["pingTimeoutMs"]
            del self.config_content["staticConfig"]["mqttConnection"][
                "sessionExpiryIntervalSeconds"
            ]

            self.config_content["staticConfig"]["mqttConnection"][
                "connectionType"
            ] = "iotGreengrassV2"

            greengrass_jar_path = Path(os.environ["TEST_GREENGRASS_JAR_PATH"]).absolute()
            if not greengrass_jar_path.exists():
                raise RuntimeError(f"Greengrass jar not found at {greengrass_jar_path}")

            self._greengrass_helper = GreengrassHelper(
                tmp_path=tmp_path,
                subprocess_helper=self.subprocess_helper,
                greengrass_jar_path=greengrass_jar_path,
                greengrass_nucleus_config_template_path=Path(
                    self.script_path + "greengrass_nucleus_config.yaml"
                ),
                aws_thing=self._aws_thing,
                fwe_recipe_template_path=Path(
                    self.script_path
                    + "../../../tools/greengrassV2/recipes/com.amazon.aws.IoTFleetWise-1.0.0.json"
                ),
                enable_aws_credentials=ros2_enabled,
                network_namespace=self.network_namespace,
                keep_alive_interval_seconds=keep_alive_interval_seconds,
                ping_timeout_ms=ping_timeout_ms,
                session_expiry_interval_seconds=session_expiry_interval_seconds,
            )
        else:
            self.config_content["staticConfig"]["mqttConnection"]["endpointUrl"] = self.endpoint
            self.config_content["staticConfig"]["mqttConnection"]["certificateFilename"] = self.cert
            self.config_content["staticConfig"]["mqttConnection"]["privateKeyFilename"] = self.key

            if keep_alive_interval_seconds is None:
                del self.config_content["staticConfig"]["mqttConnection"][
                    "keepAliveIntervalSeconds"
                ]
            else:
                self.config_content["staticConfig"]["mqttConnection"][
                    "keepAliveIntervalSeconds"
                ] = keep_alive_interval_seconds

            if ping_timeout_ms is None:
                del self.config_content["staticConfig"]["mqttConnection"]["pingTimeoutMs"]
            else:
                self.config_content["staticConfig"]["mqttConnection"][
                    "pingTimeoutMs"
                ] = ping_timeout_ms

            if session_expiry_interval_seconds is None:
                del self.config_content["staticConfig"]["mqttConnection"][
                    "sessionExpiryIntervalSeconds"
                ]
            else:
                self.config_content["staticConfig"]["mqttConnection"][
                    "sessionExpiryIntervalSeconds"
                ] = session_expiry_interval_seconds

        self.config_content["staticConfig"]["mqttConnection"][
            "iotFleetWiseTopicPrefix"
        ] = iotfleetwise_topic_prefix
        self.config_content["staticConfig"]["mqttConnection"][
            "commandsTopicPrefix"
        ] = self.commands_topic_prefix

        # The path has multiple dir levels based on the current test running, so it won't exist
        # in the target board.
        self.subprocess_helper_as_current_user.target_check_call(["mkdir", "-p", str(self.runpath)])

        self.persistency_folder = ""
        if persistency_file:
            self.persistency_folder = self.runpath / "persistency"
            self.subprocess_helper_as_current_user.target_check_call(
                ["mkdir", "-p", str(self.persistency_folder)]
            )
            self.config_content["staticConfig"]["persistency"]["persistencyPath"] = str(
                self.persistency_folder
            )
        else:
            del self.config_content["staticConfig"]["persistency"]
        if enable_remote_profiler:
            if "remoteProfilerDefaultValues" not in self.config_content["staticConfig"]:
                self.config_content["staticConfig"]["remoteProfilerDefaultValues"] = {}
            self.config_content["staticConfig"]["remoteProfilerDefaultValues"][
                "loggingUploadLevelThreshold"
            ] = "Warning"
            # Note that standard CloudWatch metrics have 60 seconds granularity. So even though we
            # are setting a period shorter than 60 seconds, it might still take 1 or 2 minutes for
            # the metric to appear on CloudWatch. We just want to make FWE emit the metrics as soon
            # as possible to reduce the time to wait.
            self.config_content["staticConfig"]["remoteProfilerDefaultValues"][
                "metricsUploadIntervalMs"
            ] = 10000
            self.config_content["staticConfig"]["remoteProfilerDefaultValues"][
                "loggingUploadMaxWaitBeforeUploadMs"
            ] = 60000
            self.config_content["staticConfig"]["remoteProfilerDefaultValues"][
                "profilerPrefix"
            ] = self.fwe_id
            self.config_content["staticConfig"]["mqttConnection"][
                "metricsUploadTopic"
            ] = "aws-iot-fleetwise-metrics-upload"
            self.config_content["staticConfig"]["mqttConnection"][
                "loggingUploadTopic"
            ] = "aws-iot-fleetwise-logging-upload"

        self.config_content["staticConfig"]["internalParameters"][
            "awsSdkLogLevel"
        ] = aws_sdk_log_level.name

        if "TEST_ROLE_ALIAS" in os.environ:
            self.config_content["staticConfig"]["credentialsProvider"][
                "endpointUrl"
            ] = self.creds_endpoint
            self.config_content["staticConfig"]["credentialsProvider"]["roleAlias"] = os.environ[
                "TEST_ROLE_ALIAS"
            ]

        # To decrease reaction time for automated test:
        self.config_content["staticConfig"]["threadIdleTimes"]["inspectionThreadIdleTimeMs"] = 50
        self.config_content["staticConfig"]["threadIdleTimes"]["socketCANThreadIdleTimeMs"] = 50
        self.config_content["staticConfig"]["threadIdleTimes"]["canDecoderThreadIdleTimeMs"] = 50
        self.config_content["staticConfig"]["publishToCloudParameters"][
            "collectionSchemeManagementCheckinIntervalMs"
        ] = self.CHECKIN_PERIOD

        running_in_parallel = os.environ.get("PYTEST_XDIST_WORKER") is not None
        if self.is_hil:
            if running_in_parallel:
                raise RuntimeError("Parallel execution is not supported with HIL")
            can_interfaces = ["can0", "can1", "llcecan0", "llcecan1"]
        elif self.number_channels > 1:
            if running_in_parallel:
                raise RuntimeError(
                    "Parallel execution is not supported when using more than 1 channel"
                )
            can_interfaces = ["vcan0", "vcan1", "vcan2", "vcan3"]
        else:
            can_interfaces = [f"vcan{worker_number}"]

        if self.number_channels > len(can_interfaces):
            raise RuntimeError(
                "Number of channels greater than max supported: %d vs %d"
                % (self.number_channels, len(can_interfaces))
            )
        # The target CAN interface is the one present in the machine where FWE is running.
        self.target_can_interface = can_interfaces[0]
        # The local CAN interface is the one present in the machine where the tests are running.
        # For a HIL setup it will depend on how many boards the driver machine is connected to.
        self.local_can_interface = (
            os.environ["TEST_HIL_DRIVER_CAN_INTERFACE"]
            if self.is_hil
            else self.target_can_interface
        )
        found_can_interface = {}
        for interface in self.config_content["networkInterfaces"]:
            if interface["type"] == "canInterface":
                found_can_interface = interface
                interface["canInterface"]["interfaceName"] = (
                    self.target_can_interface
                    if not self.network_namespace
                    else self.network_namespace.get_can_dev()
                )
            elif (
                interface["type"] == "obdInterface" and self.use_uds_dtc_generic_collection is False
            ):
                interface["obdInterface"]["interfaceName"] = (
                    self.target_can_interface
                    if not self.network_namespace
                    else self.network_namespace.get_can_dev()
                )
                # enable OBD
                if self.high_load:
                    interface["obdInterface"]["pidRequestIntervalSeconds"] = 1
                    interface["obdInterface"]["dtcRequestIntervalSeconds"] = 1
                else:
                    interface["obdInterface"]["pidRequestIntervalSeconds"] = 5
                    interface["obdInterface"]["dtcRequestIntervalSeconds"] = 5
                interface["obdInterface"]["broadcastRequests"] = self.use_obd_broadcast

        if self.network_namespace and self.number_channels > 1:
            raise Exception("Network namespace and multiple channels not supported")
        can_gateway = CanGateway()
        for c in range(1, self.number_channels):
            interface_to_add = copy.deepcopy(found_can_interface)
            interface_to_add["canInterface"]["interfaceName"] = can_interfaces[c]
            interface_to_add["interfaceId"] = str(int(found_can_interface["interfaceId"]) + c)
            if not self.is_hil:
                can_gateway.add_rule(
                    source_channel=found_can_interface["canInterface"]["interfaceName"],
                    destination_channel=interface_to_add["canInterface"]["interfaceName"],
                )
            self.config_content["networkInterfaces"].append(interface_to_add)

        self.original_cyclonedds_config_file_uri = os.environ.get("CYCLONEDDS_URI", "")
        self.original_ros2_rmw_implementation = os.environ.get("RMW_IMPLEMENTATION", "")
        if self.ros2_enabled:
            ros2_interface = {}
            ros2_interface["interfaceId"] = "10"
            ros2_interface["type"] = "ros2Interface"
            ros2_interface["ros2Interface"] = {}
            ros2_interface["ros2Interface"]["subscribeQueueLength"] = 100
            ros2_interface["ros2Interface"]["executorThreads"] = 2
            ros2_interface["ros2Interface"]["introspectionLibraryCompare"] = "ErrorAndFail"
            self.config_content["networkInterfaces"].append(ros2_interface)
            if raw_data_buffer_config is None:
                raw_data_buffer_config = RawDataBufferConfig()
            self.config_content["staticConfig"]["visionSystemDataCollection"][
                "rawDataBuffer"
            ] = raw_data_buffer_config.to_config_dict()

            self.cyclonedds_config_file_path = ""

            if self.is_hil:
                if enable_network_namespace:
                    raise RuntimeError("CycloneDDS not supported for network namespace on HIL")
                cyclonedds_config_file = "cyclonedds_hil.xml"
            elif enable_network_namespace:
                cyclonedds_config_file = "cyclonedds_network_namespace.xml"
            else:
                cyclonedds_config_file = "cyclonedds.xml"

            with open(self.script_path + cyclonedds_config_file) as f:
                updated_config = f.read().replace(
                    "NETWORK_INTERFACE_ADDRESS_PLACEHOLDER", f"10.200.{worker_number+1}.0"
                )

            self.cyclonedds_config_file_path = str(tmp_path / cyclonedds_config_file)
            with open(self.cyclonedds_config_file_path, "w") as f:
                f.write(updated_config)

            with open(self.script_path + ros2_config_file) as f:
                updated_config = f.read().replace(
                    "ROS2_TOPIC_NUMBER_PLACEHOLDER", str(worker_number)
                )

            self.ros2_config_file = tmp_path / ros2_config_file
            with open(self.ros2_config_file, "w") as f:
                f.write(updated_config)

            with open(self.script_path + "ros_vals_default.json") as f:
                updated_config = f.read().replace(
                    "ROS2_TOPIC_NUMBER_PLACEHOLDER", str(worker_number)
                )

            self.ros2_vals_file = tmp_path / "ros_vals_default.json"
            with open(self.ros2_vals_file, "w") as f:
                f.write(updated_config)

            with open(self.script_path + "gen/vision-system-data-decoder-manifest.json") as f:
                updated_config = f.read().replace(
                    "ROS2_TOPIC_NUMBER_PLACEHOLDER", str(worker_number)
                )

            self.vision_system_data_decoder_manifest_file = (
                tmp_path / "vision-system-data-decoder-manifest.json"
            )
            with open(self.vision_system_data_decoder_manifest_file, "w") as f:
                f.write(updated_config)

        if self.can_commands_enabled:
            self.config_content["networkInterfaces"].append(
                {
                    "interfaceId": "CAN_ACTUATORS",
                    "type": "canCommandInterface",
                    "canCommandInterface": {
                        "interfaceName": self.target_can_interface,
                    },
                }
            )

        if (
            self.use_can_to_someip
            or self.someip_commands_enabled
            or self.use_someip_collection
            or self.use_someip_device_shadow
        ):
            someip_app_name_can_bridge = f"someipToCanBridge{worker_number}"
            someip_app_name_collection = f"someipCollection{worker_number}"
            someip_app_name_command = f"someipCommand{worker_number}"
            someip_app_name_device_shadow = f"someipDeviceShadow{worker_number}"
            if self.use_can_to_someip:
                if not someip_instance_id_can:
                    raise RuntimeError("someipigen instance not provided")
                # Replace CAN interface with SOME/IP to CAN bridge interface
                self.config_content["networkInterfaces"][0] = {
                    "interfaceId": "1",
                    "type": "someipToCanBridgeInterface",
                    "someipToCanBridgeInterface": {
                        "someipServiceId": SOMEIP_SERVICE_ID_CAN,
                        "someipInstanceId": someip_instance_id_can,
                        "someipEventId": "0x8778",
                        "someipEventGroupId": "0x5555",
                        "someipApplicationName": someip_app_name_can_bridge,
                    },
                }
            if self.someip_commands_enabled:
                if not someipigen_instance:
                    raise RuntimeError("someipigen instance not provided")
                self.config_content["networkInterfaces"].append(
                    {
                        "interfaceId": "SOMEIP",
                        "type": "someipCommandInterface",
                        "someipCommandInterface": {
                            "someipApplicationName": someip_app_name_command,
                            "someipInstance": someipigen_instance,
                        },
                    }
                )
            if self.use_someip_collection:
                if not someipigen_instance:
                    raise RuntimeError("someipigen instance not provided")
                self.config_content["networkInterfaces"].append(
                    {
                        "interfaceId": "SOMEIP",
                        "type": "someipCollectionInterface",
                        "someipCollectionInterface": {
                            "someipApplicationName": someip_app_name_collection,
                            "someipInstance": someipigen_instance,
                            "cyclicUpdatePeriodMs": 500,
                        },
                    }
                )
            if self.use_someip_device_shadow:
                if not someip_device_shadow_editor_instance:
                    raise RuntimeError("someip_device_shadow_editor instance not provided")
                self.config_content["staticConfig"]["deviceShadowOverSomeip"] = {
                    "someipApplicationName": someip_app_name_device_shadow,
                    "someipInstance": someip_device_shadow_editor_instance,
                }

            self.vsomeip_cfg_file_fwe = create_fwe_config(
                self.runpath,
                someip_app_name_can_bridge=someip_app_name_can_bridge,
                someip_app_name_collection=someip_app_name_collection,
                someip_app_name_command=someip_app_name_command,
                someip_app_name_device_shadow=someip_app_name_device_shadow,
                worker_number=self.worker_number,
            )

        if self.use_uds_dtc_generic_collection:
            self.config_content["networkInterfaces"].append(
                {
                    "interfaceId": "UDS_DTC",
                    "type": "exampleUDSInterface",
                    "exampleUDSInterface": {
                        "configs": [
                            {
                                "targetAddress": "0x01",
                                "name": "ECM",
                                "can": {
                                    "interfaceName": self.target_can_interface,
                                    "functionalAddress": "0x7DF",
                                    "physicalRequestID": "0x7E0",
                                    "physicalResponseID": "0x7E8",
                                },
                            },
                            {
                                "targetAddress": "0x02",
                                "name": "TCM",
                                "can": {
                                    "interfaceName": self.target_can_interface,
                                    "functionalAddress": "0x7DF",
                                    "physicalRequestID": "0x7E1",
                                    "physicalResponseID": "0x7E9",
                                },
                            },
                        ]
                    },
                }
            )
            # Increase fetch throttling for high load test
            self.config_content["staticConfig"]["internalParameters"][
                "minFetchTriggerIntervalMs"
            ] = 3000

        if self.use_named_signal_data_source:
            self.config_content["networkInterfaces"].append(
                {
                    "interfaceId": "NAMED_SIGNAL",
                    "type": "namedSignalInterface",
                }
            )

        if self.use_script_engine:
            self.config_content["staticConfig"]["scriptEngine"] = {
                "bucketRegion": os.environ["TEST_S3_BUCKET_REGION"],
                "maxConnections": 2,
                "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
            }

        self.background_metrics = background_metrics
        if self.background_metrics:
            self.config_content["staticConfig"]["internalParameters"][
                "metricsCyclicPrintIntervalMs"
            ] = 60000

        if extra_static_config is not None:
            self.config_content["staticConfig"].update(extra_static_config)

        log.info(self.config_content)

        with open(self.config_file, "w") as write_file:
            json.dump(
                self.config_content,
                write_file,
                indent=4,
            )
        dbc_file = (
            self.script_path + "can_fd.dbc" if self.use_fd else self.script_path + "sample.dbc"
        )
        custom_decoder_json_files = custom_decoder_json_files or []
        node_json_files = node_json_files or []
        if self.use_uds_dtc_generic_collection:
            custom_decoder_json_files.append(
                self.script_path + "../../../tools/cloud/custom-decoders-uds-dtc.json"
            )
            node_json_files.append(
                self.script_path + "../../../tools/cloud/custom-nodes-uds-dtc.json"
            )
        if self.someip_commands_enabled or self.use_someip_collection:
            custom_decoder_json_files.append(
                self.script_path + "../../../tools/cloud/custom-decoders-someip.json"
            )
            node_json_files.append(
                self.script_path + "../../../tools/cloud/custom-nodes-someip.json"
            )
        if self.can_commands_enabled:
            custom_decoder_json_files.append(
                self.script_path + "../../../tools/cloud/custom-decoders-can-actuators.json"
            )
            node_json_files.append(
                self.script_path + "../../../tools/cloud/custom-nodes-can-actuators.json"
            )
        self.proto_factory = ProtoFactory(
            can_dbc_file=dbc_file,
            obd_json_file=self.script_path + "obdPidDecoderManifestDemo.json",
            guess_signal_types=True,
            number_of_can_channels=self.number_channels,
            vision_system_data_json_file=self.vision_system_data_decoder_manifest_file
            if self.ros2_enabled
            else None,
            custom_decoder_json_files=custom_decoder_json_files,
            node_json_files=node_json_files,
        )
        self.start_persistency_folder_size = self.get_folder_used_bytes(self.persistency_folder)
        self.start_file_system_used = self.get_total_file_system_space_used()

    def is_valgrind_enabled(self):
        return self.memcheck or self.massif or self.callgrind

    def _background_metrics_uploader(self, period_ms):
        log.info(
            "start background metrics thread and trigger every "
            + str(period_ms / 1000.0)
            + " seconds"
        )
        self.background_metrics_log_file_handle = open(self.fwe_logfile_path)
        while self.background_metrics:
            time.sleep(period_ms / 1000.0)
            if self.background_metrics:
                # In case there are transient errors (e.g. network) we don't want the thread to be
                # killed, so ensure all exceptions are caught.
                try:
                    self.get_process_info()
                except Exception:
                    log.exception("Failed to get process info")

                try:
                    self.add_file_system_metrics()
                except Exception:
                    log.exception("Failed to get file system metrics")

                try:
                    self.extract_log_info(self.background_metrics_log_file_handle)
                except Exception:
                    log.exception("Failed to extract log info")

                try:
                    self.upload_all_metrics()
                except Exception:
                    log.exception("Failed to upload metrics")

                log.info("periodic metrics upload done")

    def start_fwe(
        self, wait_for_startup=True, faketime_start_after_seconds=5, faketime_time_spec="-2d"
    ):
        """Starts FWE Executable in a new process"""
        self.fwe_logfile_path = self.runpath / "fwe.log"
        self.fwe_logfile = open(self.fwe_logfile_path, "a")

        log.info(f"start iotfleetwise ({self.binary_path}) with config: {self.config_file}")
        # We reset the credentials variable because FWE shouldn't inherit them. For accessing any
        # AWS, FWE should use the IoT credentials provider or Greengrass' TokenExchangeService
        # component.
        extra_env = {"AWS_ACCESS_KEY_ID": "", "AWS_SECRET_ACCESS_KEY": "", "AWS_SESSION_TOKEN": ""}
        cmd_list = [self.binary_path, self.config_file]
        # The actual executable could be different from the FWE executable and not easily inferred
        # from the command line. Getting the executable right is important to find the actual FWE
        # process when there is a wrapper (e.g. valgrind, faketime) or when FWE is launched by
        # another process (e.g. Greengrass).
        actual_executable = self.binary_path
        if self.use_faketime:
            cmd_list = ["faketime", "--exclude-monotonic", "-f", faketime_time_spec] + cmd_list
            extra_env["FAKETIME_START_AFTER_SECONDS"] = str(faketime_start_after_seconds)
        if self.callgrind:
            cmd_list = ["valgrind", "--tool=callgrind"] + cmd_list
        elif self.massif:
            cmd_list = ["valgrind", "--tool=massif", "--time-unit=ms"] + cmd_list
        elif self.memcheck:
            cmd_list = [
                "valgrind",
                "--tool=memcheck",
                "--leak-check=full",
                "--show-leak-kinds=all",
                "--xml=yes",
                "--gen-suppressions=all",
                f"--suppressions={Path('valgrind.supp').absolute()}",
                f"--xml-file={self.memcheck_output}",
            ] + cmd_list

        if self.is_valgrind_enabled() and not self.use_faketime:
            actual_executable = "valgrind"

        if self.is_hil and "TEST_HIL_PRESTART_CMD" in os.environ:
            if (
                self.use_can_to_someip
                or self.someip_commands_enabled
                or self.use_someip_collection
                or self.use_someip_device_shadow
            ):
                cmd_list = [f"VSOMEIP_CONFIGURATION={self.vsomeip_cfg_file_fwe}"] + cmd_list
            cmd_list = [
                "bash",
                "-c",
                f"{os.environ['TEST_HIL_PRESTART_CMD']} && {' '.join(cmd_list)}",
            ]

        if (
            self.use_can_to_someip
            or self.someip_commands_enabled
            or self.use_someip_collection
            or self.use_someip_device_shadow
        ):
            extra_env["VSOMEIP_CONFIGURATION"] = self.vsomeip_cfg_file_fwe

        if self.network_namespace and not self.use_greengrass:
            cmd_list = self.network_namespace.get_exec_list() + cmd_list
        file_list = [self.config_file, self.cert, self.key, self._aws_thing.root_ca_path]
        if (
            self.use_can_to_someip
            or self.someip_commands_enabled
            or self.use_someip_collection
            or self.use_someip_device_shadow
        ):
            file_list.append(self.vsomeip_cfg_file_fwe)

        if self.ros2_enabled:
            file_list.append(self.cyclonedds_config_file_path)
            extra_env["CYCLONEDDS_URI"] = f"file://{self.cyclonedds_config_file_path}"
        extra_env["RMW_IMPLEMENTATION"] = "rmw_cyclonedds_cpp"

        self.subprocess_helper.copy_files_to_target(list(file_list), self.runpath)

        # Use lifecycle events as a mechanism to wait for FWE to start instead or arbitrary sleeps.
        # This will make the test more robust against slow scenarios (e.g. valgrind), but it will be
        # faster in most other occasions.
        self.lifecycle_connected_events = []
        self.lifecycle_disconnected_events = []
        self.lifecycle_subscribed_events = []
        self.lifecycle_unsubscribed_events = []
        if self.use_greengrass and self._greengrass_helper:
            self._greengrass_helper.start()
            self.fwe_process = self._greengrass_helper.deploy_fwe(
                cmd_list, executable=actual_executable, extra_env=extra_env
            )
            # Read the Greengrass logs and save to a local file (for the case when this is HIL test)
            self._greengrass_log_reader_process = self.subprocess_helper.target_popen(
                [
                    "tail",
                    "-n10000",
                    "-f",
                    str(self._greengrass_helper.logs_path / "com.amazon.aws.IoTFleetWise.log"),
                ],
                executable="tail",
                stdout=self.fwe_logfile,
                stderr=subprocess.STDOUT,
            )
        else:
            self.fwe_process = self.subprocess_helper.target_popen(
                cmd_list,
                executable=actual_executable,
                stdout=self.fwe_logfile,
                stderr=subprocess.STDOUT,
                extra_env=extra_env,
            )
        if wait_for_startup:
            self.wait_for_fwe_startup()

        if self.background_metrics:
            self.background_metrics_thread = Thread(
                name="MetricsUploader", target=self._background_metrics_uploader, args=(60000,)
            )
            self.background_metrics_thread.start()

    def wait_for_fwe_startup(self):
        """Wait until FWE is ready to be interacted with

        The main thing that is needed for most tests is that FWE should be listening to the MQTT
        messages that the tests will send. So we will wait for the subscription events for all
        topics of features currently enabled.
        """
        expected_subscribed = {self.decoder_manifest_topic, self.collection_scheme_list_topic}

        if self.someip_commands_enabled or self.last_known_state_enabled:
            expected_subscribed = expected_subscribed.union(
                [
                    self.command_request_topic,
                    (
                        f"{self.commands_topic_prefix}things/{self.fwe_id}/"
                        "executions/+/response/accepted/protobuf"
                    ),
                    (
                        f"{self.commands_topic_prefix}things/{self.fwe_id}/"
                        "executions/+/response/rejected/protobuf"
                    ),
                ]
            )

        if self.last_known_state_enabled:
            expected_subscribed.add(self.last_known_state_config_topic)

        if self.use_someip_device_shadow:
            expected_subscribed.add(f"$aws/things/{self.fwe_id}/shadow/#")

        if self.use_store_and_forward:
            jobs_topic_prefix = f"$aws/things/{self.fwe_id}/jobs/"
            expected_subscribed = expected_subscribed.union(
                [
                    f"{jobs_topic_prefix}get/accepted",
                    f"{jobs_topic_prefix}get/rejected",
                    f"{jobs_topic_prefix}+/get/accepted",
                    f"{jobs_topic_prefix}+/get/rejected",
                    f"{jobs_topic_prefix}+/update/accepted",
                    f"{jobs_topic_prefix}+/update/rejected",
                    f"{jobs_topic_prefix}notify",
                    "$aws/events/job/+/cancellation_in_progress",
                ]
            )

        kwargs = {}
        if self.is_valgrind_enabled():
            # With valgrind it can easily take several seconds just for the first log line to be
            # output. Considering all the things that need to be initialized, the MQTT connection
            # attempt and the amount of topics to be subscribed it can take more than 30 seconds
            # for FWE to subscribe to all topics.
            kwargs["stop"] = stop_after_delay(60)

        for attempt in Retrying(**kwargs):
            with attempt:
                currently_subscribed = set()
                for event in self.lifecycle_subscribed_events:
                    currently_subscribed = currently_subscribed.union(event["topics"])

                assert currently_subscribed.intersection(expected_subscribed) == expected_subscribed

    def get_remote_profiler_metrics(self, start_time: datetime):
        cw = self.get_cloudwatch_client()
        response = cw.get_metric_data(
            MetricDataQueries=[
                {
                    "Id": "variableMaxSinceStartup_RFrames0_id0",  # first letter must be lowerCase
                    "MetricStat": {
                        "Metric": {
                            "Namespace": "AWSIotFleetWiseEdge",
                            "MetricName": self.fwe_id + "_variableMaxSinceStartup_RFrames0_id0",
                        },
                        "Period": 60,
                        "Stat": "Maximum",
                    },
                    "ReturnData": True,
                },
            ],
            StartTime=start_time,
            EndTime=datetime.now(),
            ScanBy="TimestampAscending",
            MaxDatapoints=100,
        )
        output = {}
        for metric in response["MetricDataResults"]:
            metric_id = metric["Id"]
            output[metric_id] = []
            number_of_values = min(len(metric["Timestamps"]), len(metric["Values"]))
            for value_counter in range(0, number_of_values):
                output[metric_id].append(
                    {
                        "timestamp": metric["Timestamps"][value_counter],
                        "value": metric["Values"][value_counter],
                    }
                )
            output[metric_id].sort(key=lambda m: m["timestamp"])
        return output

    def get_cloudwatch_client(self):
        # Boto3 caches the contents of the ~/.aws/credentials file (when it decides to use).
        # If the credentials expire and the file is updated with new ones, boto3 doesn't re-read it
        # unless we create a new session. We always create a new session without manually passing
        # the credentials because we want to use the default chain (so we can also use credentials
        # from env vars, EC2 metadata, etc.).
        session = Session()
        cw = session.client("cloudwatch")

        return cw

    def get_iot_client(self):
        session = Session()
        iot = session.client("iot")

        return iot

    def upload_all_metrics(self):
        if not self.upload_metrics:
            log.warning(
                "Not uploading any metrics to CloudWatch."
                " Please set the TEST_UPLOAD_METRICS env var if you want to upload the"
                " metrics."
            )
            return

        with self.metrics_lock:
            if len(self.metrics_not_yet_uploaded) == 0:
                return

            metrics_to_upload = self.metrics_not_yet_uploaded
            self.metrics_not_yet_uploaded = []

        # Metrics without an explicit timestamp will be assigned the upload time. But especially
        # if we have a lot of metrics to upload, requiring us to split them in different requests,
        # some metrics may be shifted by a few seconds. So we assign the same timestamp to all
        # metrics.
        timestamp = datetime.utcnow()
        for metric in metrics_to_upload:
            if "Timestamp" not in metric:
                metric["Timestamp"] = timestamp

        log.info(f"Number of metrics to be uploaded: {len(metrics_to_upload)}")
        cw = self.get_cloudwatch_client()
        chunk_size = 1000
        i = 0
        while i < len(metrics_to_upload):
            metric_data = metrics_to_upload[i : i + chunk_size]
            i += chunk_size
            cw.put_metric_data(Namespace="aws-iot-fleetwise-edge", MetricData=metric_data)

    def check_metrics_against_alarms(self):
        """Check the produced metrics against some alarms in CloudWatch

        Normally this should only be used in load tests and needs to be explicitly enabled by
        setting the TEST_CHECK_METRICS_FOR_BRANCH variable containing the branch for the alarm (NOTE
        that it is not the temporary branch that someone is working on).
        """
        if not self.branch_for_alarms:
            log.warning(
                "Not checking metrics against alarms because branch is not set."
                " Please set the TEST_CHECK_METRICS_FOR_BRANCH env var if you want to check the"
                " metrics."
            )
            return

        alarm_configs: List[Dict[str, Any]] = [
            {
                "alarm_name": (
                    f"CPUTime - {self.metrics_name} - {self.metrics_platform}"
                    f" - {self.branch_for_alarms}"
                ),
                "metric_name": "cputime_sum",
            },
            {
                "alarm_name": (
                    f"Memory - {self.metrics_name} - {self.metrics_platform}"
                    f" - {self.branch_for_alarms}"
                ),
                "metric_name": "memory_residentRam",
            },
        ]

        log.info("Getting thresholds from alarms...")
        missing_configs = {config["alarm_name"] for config in alarm_configs}
        cw_client = self.get_cloudwatch_client()
        alarms = cw_client.describe_alarms()
        threshold_breaches = []
        while alarms["MetricAlarms"] and missing_configs:
            for alarm in alarms["MetricAlarms"]:
                for config in alarm_configs:
                    if config["alarm_name"] != alarm["AlarmName"]:
                        continue
                    missing_configs.remove(config["alarm_name"])
                    log.info(f"Checking metric against alarm: {config}")
                    config["threshold"] = alarm["Threshold"]
                    config["current_value"] = self.metrics_to_check[config["metric_name"]]
                    if config["current_value"] > config["threshold"]:
                        threshold_breaches.append(config)
            if "NextToken" not in alarms:
                break
            alarms = cw_client.describe_alarms(NextToken=alarms["NextToken"])

        assert not missing_configs, "Some alarms couldn't be found"
        assert (
            not threshold_breaches
        ), "Some metrics breached the thresholds configured in the alarms"

    def add_metrics(self, name, value, unit, timestamp: Optional[datetime] = None):
        """
        upload a metrics to aws cloudwatch. The Units available are listed here under Unit:
        https://docs.aws.amazon.com/AmazonCloudWatch/latest/APIReference/API_MetricDatum.html
        """
        metric = {
            "MetricName": str(name),
            "Value": value,
            "Unit": str(unit),
            "Dimensions": [
                {"Name": "branch", "Value": self.branch},
                {"Name": "platform", "Value": self.metrics_platform},
                {
                    "Name": "test-case",
                    "Value": (
                        self.metrics_name
                        if self.metrics_name and len(self.metrics_name) > 1
                        else "unknown"
                    ),
                },
            ],
        }
        if timestamp:
            metric["Timestamp"] = timestamp
        self.metrics_output.append(metric)
        if len(self.metrics_name) > 1 and len(self.branch) > 1:
            with self.metrics_lock:
                self.metrics_not_yet_uploaded.append(metric)

    def extract_log_info(self, logfile):
        log.info("Start log file analysis")
        regex_variable = re.compile(
            r".*\'(?P<name>.*?)\'"
            r" \[(?P<id>.*?)\]"
            r" .*\[(?P<current>.*?)\]"
            r" .*\[(?P<temp_max>.*?)\]"
            r" .*\[(?P<max>.*?)\]"
        )
        regex_section = re.compile(
            r".*\'(?P<name>.*?)\'"
            r" \[(?P<id>.*?)\]"
            r" .*\[(?P<times>.*?)\]"
            r" .*\[(?P<avg_time>.*?)\]"
            r" .*\[(?P<tmp_max_time>.*?)\]"
            r" .*\[(?P<max_time>.*?)\]"
            r" .*\[(?P<avg_interval>.*?)\]"
            r" .*\[(?P<tmp_max_interval>.*?)\]"
            r" .*\[(?P<max_interval>.*?)\]"
        )
        regex_send_data = re.compile(
            r".*?\[.*?"
            r" eventID (?P<event_id>.*?)"
            r" from (?P<campaign_sync_id>.*?)"
            r" Signals:(?P<samples_count>.*?)"
            r" .*?trigger timestamp: (?P<trigger_timestamp>.*?)"
            r" .*?\]"
        )

        # Greengrass adds redundant info to each log line, for example the data and log level
        # (which will differ from FWE's level and just make the logs confusing), so we strip this
        # part.
        # This should match something like:
        # 2025-04-07T12:25:22.651Z [WARN] (Copier) com.amazon.aws.IoTFleetWise: stderr.
        regex_greengrass_log_prefix = re.compile(
            r"\d\d\d\d-\d\d-\d\dT\d\d:\d\d:\d\d.\d\d\dZ"
            r" \[\w+\] \(Copier\) com\.amazon\.aws\.IoTFleetWise: (stderr|stdout)\. "
        )

        next_lines = logfile.readlines()
        if len(next_lines) > 0:
            next_lines[0] = self._incomplete_log_line + next_lines[0]
            if next_lines[-1].endswith("\n"):
                self._incomplete_log_line = ""
            else:
                self._incomplete_log_line = next_lines.pop(-1)

        for line in next_lines:
            line = line.strip()
            if self.use_greengrass:
                line = re.sub(regex_greengrass_log_prefix, "", line)
            if "[WARN" in line and len(self.log_warnings) < 500:
                self.log_warnings.append(line)
            if "[ERROR" in line and len(self.log_errors) < 500:
                self.log_errors.append(line)
            atomic_variable = "TraceModule-ConsoleLogging-TraceAtomicVariable" in line
            if "TraceModule-ConsoleLogging-Variable" in line or atomic_variable:
                match = regex_variable.search(line)
                if not match:
                    log.error("could not parse line: " + line)
                    continue
                match.group("name")
                # We put both temp_max and max metrics because of the following:
                # - `temp_max` will have the current value for each sample, which will provide more
                # details about trends and how the metric behaves throughout the whole program
                # execution. And allows us to use CloudWatch aggregation and statistics (min, max,
                # avg) capabilities.
                # - `max` will provide a metric that always show the max value since the program
                # started (not the current value).  We could obtain the same value by using the
                # `temp_max` metric, but we may miss a data point (e.g. some badly formatted log
                # line). So we also provide the overall max especially for alarms.
                self.add_metrics(
                    "variable_" + ("atomic_" if atomic_variable else "") + match.group("name"),
                    int(match.group("temp_max")),
                    "Count",
                )
                self.add_metrics(
                    "variable_"
                    + ("atomic_" if atomic_variable else "")
                    + match.group("name")
                    + "_max",
                    int(match.group("max")),
                    "Count",
                )
            elif "TraceModule-ConsoleLogging-Section" in line:
                match = regex_section.search(line)
                if not match:
                    log.error("could not parse line: " + line)
                    continue
                match.group("name")
                self.add_metrics(
                    "section_" + match.group("name") + "_avg_time",
                    float(match.group("avg_time")),
                    "Seconds",
                )
                self.add_metrics(
                    "section_" + match.group("name") + "_max_time",
                    float(match.group("max_time")),
                    "Seconds",
                )
                self.add_metrics(
                    "section_" + match.group("name") + "_times",
                    int(match.group("times")),
                    "Count",
                )
            elif "FWE data ready to send" in line:
                match = regex_send_data.search(line)
                if not match:
                    log.error("could not parse line: " + line)
                    continue

                samples_count = int(match.group("samples_count"))
                self.collected_samples_counter += samples_count

                # Print the collected samples for investigation when there is a difference between
                # the collected and received samples metrics.
                log.debug(
                    "Samples collected by Edge"
                    f" campaign_sync_id: {match.group('campaign_sync_id')}"
                    f" samples_count: {samples_count}"
                    f" timestamp: {match.group('trigger_timestamp')}"
                )

                # Timestamp should normally be passed only when we want real time metrics.
                # In shorter tests we normally just want a single data point, so for these tests we
                # publish the metrics only when the test finishes and all the metrics should have
                # the same timestamp (upload time).
                kwargs = {}
                if self.background_metrics:
                    kwargs = {
                        "timestamp": datetime.utcfromtimestamp(
                            float(match.group("trigger_timestamp")) / 1000
                        )
                    }
                self.add_metrics("CollectedSamples", samples_count, "Count", **kwargs)

        log.info("Finished log file analysis")

    def get_folder_used_bytes(self, folder):
        if folder == "":
            return 0
        du_output = self.subprocess_helper.target_check_output(["du", "-d", "0", folder])
        used_bytes = int(du_output.split()[0]) * 1024
        return used_bytes

    def get_file_size(self, filename: Path, local=False):
        if filename == "":
            return 0
        cmd_list = ["stat", "-c", "%s", filename]
        if local:
            stat_output = self.subprocess_helper.local_check_output(cmd_list).strip()
        else:
            stat_output = self.subprocess_helper.target_check_output(cmd_list).strip()
        if stat_output.isdigit():
            return int(stat_output)
        else:
            return 0

    def get_total_file_system_space_used(self):
        # get free and used space of current directory
        df_output = self.subprocess_helper.target_check_output(["df", "-P", "./"])
        used_bytes = int(df_output.splitlines()[-1].split()[2]) * 1024
        return used_bytes

    def add_file_system_metrics(self):
        self.add_metrics(
            "filesystem_persistencyFolder",
            self.get_folder_used_bytes(self.persistency_folder)
            - self.start_persistency_folder_size,
            "Bytes",
        )
        self.add_metrics(
            "filesystem_overall",
            self.get_total_file_system_space_used() - self.start_file_system_used,
            "Bytes",
        )
        self.add_metrics(
            "filesystem_logFile", self.get_file_size(self.fwe_logfile_path, True), "Bytes"
        )  # Log file is also in hil in local file system

    def start_cpu_measurement(self):
        self.fwe_process_stat_start = self.get_process_stat()

    def stop_cpu_measurement(self):
        self.fwe_process_stat_stop = self.get_process_stat()

    def get_process_info(self):
        if not self.fwe_process:
            return
        process_id = self.fwe_process.pid
        process_stat = None
        if self.fwe_process_stat_start:
            if not self.fwe_process_stat_stop:
                self.fwe_process_stat_stop = self.get_process_stat()
            if self.fwe_process_stat_stop:
                process_stat = self.fwe_process_stat_stop - self.fwe_process_stat_start
        else:
            # If start_cpu_measurement was never called, then just calculate from when the process
            # started until now.
            process_stat = self.get_process_stat()
        memory_info = self.subprocess_helper.target_check_output(
            ["cat", f"/proc/{process_id}/status"]
        )
        map_info = self.subprocess_helper.target_check_output(["cat", f"/proc/{process_id}/maps"])
        size_output = self.subprocess_helper.target_check_output(["size", self.binary_path])
        for s in size_output.splitlines():
            p = s.split()
            if len(p) > 4 and p[3].isdigit():
                self.add_metrics("memory_staticRAMROM", int(p[3]), "Bytes")
                break
        size_sum = 0
        for line in map_info.splitlines():
            split_1 = line.split("-", 1)
            start_addr = int(split_1[0], 16)
            end_addr = int(split_1[1].split(" ", 1)[0], 16)
            size = end_addr - start_addr
            size_sum += size

        cpu_time_percent = (
            (process_stat.user_time_seconds + process_stat.kernel_time_seconds)
            / process_stat.run_time_seconds
        ) * 100
        log.info(f"Overall process CPU usage: {cpu_time_percent:.2f}%")
        self.add_metrics("cputime_sum", cpu_time_percent, "Percent")
        if not self.background_metrics:
            # Save the metric separately so that we can check against alarms
            self.metrics_to_check["cputime_sum"] = cpu_time_percent

        cpu_time_by_thread = {}
        for thread_stat in sorted(process_stat.thread_stats.values(), key=lambda x: x.tid):
            # Intentionally divide by the process run time instead of the thread run time
            # because we are measuring how much CPU each thread consumed when compared to the whole
            # process.
            thread_cpu_time_percent = (
                (thread_stat.user_time_seconds + thread_stat.kernel_time_seconds)
                / process_stat.run_time_seconds
            ) * 100
            log.info(f"CPU usage for thread {thread_stat.name}: {thread_cpu_time_percent:.2f}%")
            if thread_stat.name in cpu_time_by_thread:
                log.warning(
                    f"Repeated thread name when extracting metrics: '{thread_stat.name}'."
                    " Most likely the thread is not being renamed when FWE creates it."
                    " Unique thread names are necessary to reliably generate CPU metrics by thread."
                    " Merging CPU metric for the threads with same name."
                )
                cpu_time_by_thread[thread_stat.name] += thread_cpu_time_percent
            else:
                cpu_time_by_thread[thread_stat.name] = thread_cpu_time_percent

        for thread_name, thread_cpu_time_percent in cpu_time_by_thread.items():
            self.add_metrics("cputime_thread_" + thread_name, thread_cpu_time_percent, "Percent")

        for line in memory_info.splitlines():
            s = line.split()
            if len(s) > 1 and "VmHWM" in s[0]:
                resident_ram_bytes = int(s[1]) * 1000
                self.add_metrics("memory_residentRam", resident_ram_bytes, "Bytes")
                if not self.background_metrics:
                    # Save the metric separately so that we can check against alarms
                    self.metrics_to_check["memory_residentRam"] = resident_ram_bytes

        log.info("Sum of all sizes:" + str(size_sum))

    def get_process_stat(self) -> Optional[ProcessStat]:
        if not self.fwe_process:
            return None

        # Run everything in a single command to minimize delays between commands, which could
        # mess up our calculations
        proc_stat_command = [
            "cat",
            # Include the current system time so we can calculate how long the process has been
            # running. The /stat below only includes the start time, not the elapsed time.
            "/proc/uptime",
            # We need to also include the process stat as it is not included in the tasks
            f"/proc/{self.fwe_process.pid}/stat",
            f"/proc/{self.fwe_process.pid}/task/*/stat",
        ]
        output = self.subprocess_helper.target_check_output(proc_stat_command, shell=True)
        lines = output.strip().splitlines()
        system_uptime_seconds = float(lines[0].split()[0])
        clock_ticks_per_second = int(
            self.subprocess_helper.target_check_output(["getconf", "CLK_TCK"]).strip()
        )

        process_stat = None
        for line in lines[1:]:
            # Add a dummy first element because the documentation starts with index 1, so we can use
            # the same numbers as in the documentation:
            # https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html
            stats = [""] + line.split()

            pid_or_tid = int(stats[1])
            name = stats[2].strip("()")
            user_time_seconds = int(stats[14]) / clock_ticks_per_second
            kernel_time_seconds = int(stats[15]) / clock_ticks_per_second
            start_time_seconds = int(stats[22]) / clock_ticks_per_second

            run_time_seconds = system_uptime_seconds - start_time_seconds

            if process_stat is None:
                process_stat = ProcessStat(
                    pid=pid_or_tid,
                    name=name,
                    user_time_seconds=user_time_seconds,
                    kernel_time_seconds=kernel_time_seconds,
                    start_time_seconds=start_time_seconds,
                    run_time_seconds=run_time_seconds,
                    thread_stats={},
                )
            else:
                process_stat.thread_stats[pid_or_tid] = ThreadStat(
                    tid=pid_or_tid,
                    name=name,
                    user_time_seconds=user_time_seconds,
                    kernel_time_seconds=kernel_time_seconds,
                    start_time_seconds=start_time_seconds,
                    run_time_seconds=run_time_seconds,
                )
        if process_stat is None:
            raise RuntimeError(f"Could not parse the process stat:\n{output}")

        return process_stat

    def stop_fwe(self):
        # End background metrics upload before doing final analysis and upload
        log_file_handler = (
            self.background_metrics_log_file_handle
            if self.background_metrics and hasattr(self, "background_metrics_log_file_handle")
            else open(self.fwe_logfile_path)
        )
        self.background_metrics = False
        self.get_process_info()
        self.add_file_system_metrics()

        process_id = None
        process_return_code = None
        if self.fwe_process:
            process_id = self.fwe_process.pid
            process_return_code = stop_process_and_wait(
                self.subprocess_helper,
                self.fwe_process,
                self.runpath,
                is_valgrind=self.is_valgrind_enabled(),
            )

        if self._greengrass_helper:
            self._greengrass_helper.stop()
        if self._greengrass_log_reader_process:
            self._greengrass_log_reader_process.terminate()
            self._greengrass_log_reader_process.wait(timeout=30)

        self.fwe_logfile.close()

        log.info("Assuming Shutdown complete")
        self.extract_log_info(log_file_handler)
        log_file_handler.close()
        log.info("Disconnecting from MQTT")
        if self.mqtt_client is not None:
            self.mqtt_client.stop()
            self.stop_future.result(timeout=30)
        self.upload_all_metrics()

        if len(self.metrics_name) > 1:

            class CustomEncoder(json.JSONEncoder):
                def default(self, obj):
                    if isinstance(obj, datetime):
                        return obj.isoformat()
                    return json.JSONEncoder.default(self, obj)

            with open(self.runpath / "metrics.json", "w") as write_file:
                json.dump(self.metrics_output, write_file, indent=4, cls=CustomEncoder)

        if not self.fwe_process:
            log.warning("FWE process never started")
            return

        if process_return_code is None:
            raise RuntimeError(
                f"Process did not exit gracefully {process_id=}."
                " Stacktraces for all threads have been printed multiple times a few seconds apart"
                " before sending SIGKILL. See the captured stdout to find them. You can compare the"
                " multiple stacktraces to check which threads were still making progress and which"
                " ones were really stuck."
                " If a core dump file was generated, you can inspect it by running:"
                f" gdb {self.binary_path} <CORE_DUMP_FILE>"
            )

        if self.memcheck and self._has_memcheck_errors(self.memcheck_output):
            raise RuntimeError(
                "There are valgrind memcheck failures, please check the log:"
                f" {self.memcheck_output}"
            )

        if process_return_code != 0:
            raise RuntimeError(
                f"Process exited with non-zero code {process_id=} {process_return_code=}"
            )

    def _has_memcheck_errors(self, path: Path):
        """review the output file of the memcheck"""
        try:
            tree = ElementTree.parse(path)
        except ElementTree.ParseError:
            raise RuntimeError(
                "valgrind xml output file cannot be parsed. Process was likely terminated,"
                f" check the output file {self.memcheck_output}"
            )
        root = tree.getroot()
        return root.find("errorcounts").find("*") is not None

    def _on_message_received(self, data: mqtt5.PublishReceivedData):
        """Common callback for received messages from cloud"""

        publish_packet = data.publish_packet
        topic = publish_packet.topic
        payload = publish_packet.payload
        try:
            max_bytes_to_log = 50
            # Fully log the lifecycle events since their are json, not very frequent and can be
            # useful to investigate what FWE is doing.
            if not topic.startswith("$aws/events") and len(payload) > max_bytes_to_log:
                publish_packet.payload = payload[:max_bytes_to_log] + b"...<TRUNCATED>..."
            log.debug(
                f"Received message from topic '{topic}' with"
                f" {len(payload)} bytes {publish_packet=}"
            )
            # can-data topic
            if topic == self.can_data_topic:
                try:
                    payload = snappy.decompress(payload)
                except Exception:
                    pass
                received_data = ReceivedData(
                    payload,
                    self.proto_factory.decoder_manifest_proto,
                    self.proto_factory.signal_name_to_id,
                )

                samples_count = sum(len(v) for v in received_data.signals.values())

                # Also print some info for investigation when the collected and received samples
                # metrics don't match.
                log.debug(
                    "Received message with samples"
                    f" campaign_sync_id: {received_data.campaign_sync_id}"
                    f" samples_count: {samples_count}"
                    f" timestamp: {received_data.receive_timestamp}"
                )

                metrics_kwargs = {}
                # When not emitting background metrics, timestamp is omitted to the upload time is
                # considered.
                if self.background_metrics:
                    metrics_kwargs["timestamp"] = datetime.utcfromtimestamp(
                        received_data.receive_timestamp / 1000
                    )
                self.add_metrics("ReceivedDataMessages", 1, "Count", **metrics_kwargs)
                self.add_metrics("ReceivedDataBytes", len(payload), "Bytes", **metrics_kwargs)
                self.add_metrics("ReceivedSamples", samples_count, "Count", **metrics_kwargs)
                self.add_metrics(
                    "UploadedS3Objects",
                    len(received_data.uploaded_s3_objects),
                    "Count",
                    **metrics_kwargs,
                )
                self.receive_data_counter += 1
                self.receive_data_bytes += len(payload)
                self.received_samples_counter += samples_count
                # to avoid ram overflow stop adding signals after 3GB
                if resource.getrusage(resource.RUSAGE_SELF).ru_maxrss < self.ram_limit:
                    if (len(self.received_data) >= 1) and (
                        self.received_data[-1].event_id == received_data.event_id
                    ):
                        self.received_data[-1].append(received_data)
                    else:
                        self.received_data.append(received_data)
            # checkin topic
            elif topic == self.checkin_topic:
                if resource.getrusage(resource.RUSAGE_SELF).ru_maxrss < self.ram_limit:
                    # Append the received checkins
                    self.received_checkins.append(ReceivedCheckin(payload))
            elif re.match(self.command_response_topic.replace("+", ".+?"), topic):
                self.received_command_responses.append(ReceivedCommandResponse(payload))
            elif topic == self.last_known_state_data_topic:
                self.received_last_known_state.append(
                    ReceivedLastKnownStateData(payload, self.proto_factory.signal_name_to_id)
                )
            elif topic == self.lifecycle_connected_topic:
                if resource.getrusage(resource.RUSAGE_SELF).ru_maxrss < self.ram_limit:
                    self.lifecycle_connected_events.append(json.loads(payload))
            elif topic == self.lifecycle_disconnected_topic:
                if resource.getrusage(resource.RUSAGE_SELF).ru_maxrss < self.ram_limit:
                    self.lifecycle_disconnected_events.append(json.loads(payload))
            elif topic == self.lifecycle_subscribed_topic:
                if resource.getrusage(resource.RUSAGE_SELF).ru_maxrss < self.ram_limit:
                    self.lifecycle_subscribed_events.append(json.loads(payload))
            elif topic == self.lifecycle_unsubscribed_topic:
                if resource.getrusage(resource.RUSAGE_SELF).ru_maxrss < self.ram_limit:
                    self.lifecycle_unsubscribed_events.append(json.loads(payload))
            else:
                raise Exception(
                    f"Received data on topic {topic} and there is no callback functionality to"
                    " process it"
                )
        except Exception:
            log.exception("Error when handling received message")

    def connect_to_cloud(self):
        self.event_loop_group = io.EventLoopGroup(1)
        self.host_resolver = io.DefaultHostResolver(self.event_loop_group)
        self.client_bootstrap = io.ClientBootstrap(self.event_loop_group, self.host_resolver)
        self.stop_future = Future()
        connect_future = Future()

        def on_lifecycle_attempting_connect(data: mqtt5.LifecycleAttemptingConnectData):
            log.info(f"Attempting to connect with MQTT {data=}")

        def on_lifecycle_connection_success(data: mqtt5.LifecycleConnectSuccessData):
            log.info(f"MQTT connection succeeded {data=}")
            connect_future.set_result(True)

        def on_lifecycle_connection_failure(data: mqtt5.LifecycleConnectFailureData):
            log.error(f"MQTT connection failed {data=}")
            connect_future.set_result(False)

        def on_lifecycle_disconnection(data: mqtt5.LifecycleDisconnectData):
            log.info(f"MQTT disconnected {data=}")

        def on_lifecycle_stopped(data: mqtt5.LifecycleStoppedData):
            log.info(f"MQTT client stopped {data=}")
            self.stop_future.set_result(None)

        log.info(f"Connecting to {self.endpoint} with client ID '{self.client_id}'...")
        self.mqtt_client = mqtt5_client_builder.mtls_from_path(
            endpoint=self.endpoint,
            client_id=self.client_id,
            cert_filepath=self.cert,
            pri_key_filepath=self.key,
            client_bootstrap=self.client_bootstrap,
            on_publish_received=self._on_message_received,
            on_lifecycle_stopped=on_lifecycle_stopped,
            on_lifecycle_attempting_connect=on_lifecycle_attempting_connect,
            on_lifecycle_connection_success=on_lifecycle_connection_success,
            on_lifecycle_connection_failure=on_lifecycle_connection_failure,
            on_lifecycle_disconnection=on_lifecycle_disconnection,
            client_options=mqtt5.ClientOptions(
                host_name=self.endpoint,
                ping_timeout_ms=5000,
                connect_options=mqtt5.ConnectPacket(
                    keep_alive_interval_sec=6,
                    session_expiry_interval_sec=600,
                ),
                session_behavior=mqtt5.ClientSessionBehaviorType.REJOIN_POST_SUCCESS,
            ),
        )
        self.mqtt_client.start()

        if not connect_future.result(timeout=30):
            raise RuntimeError("MQTT connection failed")
        log.info("Connected!")

        # Subscriptions
        # ================================================================
        self.subscribe_to_topic(self.can_data_topic)
        self.subscribe_to_topic(self.checkin_topic)
        # lifecycle events, so that we can better control the tests and know for sure when FWE is
        # ready
        self.subscribe_to_topic(self.lifecycle_connected_topic)
        self.subscribe_to_topic(self.lifecycle_disconnected_topic)
        self.subscribe_to_topic(self.lifecycle_subscribed_topic)
        self.subscribe_to_topic(self.lifecycle_unsubscribed_topic)
        if self.someip_commands_enabled or self.last_known_state_enabled:
            self.subscribe_to_topic(self.command_response_topic)
        if self.last_known_state_enabled:
            self.subscribe_to_topic(self.last_known_state_data_topic)
        # ================================================================

    def subscribe_to_topic(self, topic_name):
        log.info(f"Subscribing to topic '{topic_name}'...")
        subscribe_future = self.mqtt_client.subscribe(
            subscribe_packet=mqtt5.SubscribePacket(
                subscriptions=[
                    mqtt5.Subscription(topic_filter=topic_name, qos=mqtt5.QoS.AT_LEAST_ONCE)
                ]
            )
        )
        subscribe_result: mqtt5.SubackPacket = subscribe_future.result(timeout=30)
        log.info(f"Subscribed with {subscribe_result=}")

    def send_collection_schemes(self, collection_schemes, silent=False, corrupted=False):
        self.proto_factory.create_collection_schemes_proto(collection_schemes)
        self.send_collection_schemes_proto(self.proto_factory.collection_schemes_proto, corrupted)

    def send_collection_schemes_proto(self, collection_schemes_proto, corrupted=False):
        if self.mqtt_client is None:
            raise RuntimeError("MQTT client not connected")

        outstring = collection_schemes_proto.SerializeToString()
        if corrupted:
            log.info("Only publish first half of protocol buffer")
            outstring = outstring[: int(len(outstring) / 2)]
        with open(self.runpath / "collection-scheme-list.bin", "wb") as write_file:
            write_file.write(outstring)
        with open(self.runpath / "collection-scheme-list.json", "w") as write_file:
            write_file.write(MessageToJson(collection_schemes_proto))
        self.add_metrics("SentCollectionSchemesBytes", len(outstring), "Bytes")
        future = self.mqtt_client.publish(
            mqtt5.PublishPacket(
                topic=self.collection_scheme_list_topic,
                payload=outstring,
                qos=mqtt5.QoS.AT_LEAST_ONCE,
            )
        )

        future.result()
        log.debug(
            "send_collection_schemes on topic "
            + self.collection_scheme_list_topic
            + " with byte length: "
            + str(len(outstring))
        )

    def send_decoder_manifest(self, decoder_manifest_proto=None):
        if self.mqtt_client is None:
            raise RuntimeError("MQTT client not connected")

        if not decoder_manifest_proto:
            decoder_manifest_proto = self.proto_factory.decoder_manifest_proto
        outstring = decoder_manifest_proto.SerializeToString()
        # Write to file to reuse it for interactive testing
        with open(self.runpath / "current_decoder_manifest.bin", "wb") as write_file:
            write_file.write(outstring)
        with open(self.runpath / "current_decoder_manifest.json", "w") as write_file:
            write_file.write(MessageToJson(decoder_manifest_proto))
        self.add_metrics("SentDecoderManifestBytes", len(outstring), "Bytes")
        future = self.mqtt_client.publish(
            mqtt5.PublishPacket(
                topic=self.decoder_manifest_topic,
                payload=outstring,
                qos=mqtt5.QoS.AT_LEAST_ONCE,
            )
        )

        future.result()
        log.debug(
            "send_decoder_manifest on topic "
            + self.decoder_manifest_topic
            + " with byte length: "
            + str(len(outstring))
        )

    def send_command_request_proto(self, command_request_proto: command_request_pb2.CommandRequest):
        if self.mqtt_client is None:
            raise RuntimeError("MQTT client not connected")

        topic = (
            f"{self.commands_topic_prefix}things/{self.fwe_id}/executions/"
            f"{command_request_proto.command_id}/request/protobuf"
        )

        outstring = command_request_proto.SerializeToString()
        with open(self.runpath / "command-request.bin", "wb") as write_file:
            write_file.write(outstring)
        with open(self.runpath / "command-request.json", "w") as write_file:
            write_file.write(MessageToJson(command_request_proto))
        self.add_metrics("SentCommandRequestBytes", len(outstring), "Bytes")
        future = self.mqtt_client.publish(
            mqtt5.PublishPacket(
                topic=topic,
                payload=outstring,
                qos=mqtt5.QoS.AT_LEAST_ONCE,
            )
        )

        future.result()
        log.debug(
            "send_command_request on topic " + topic + " with byte length: " + str(len(outstring))
        )

    def send_actuator_command_request(
        self,
        command_id: str,
        fqn: str,
        arg,
        arg_type: SignalType,
        timeout: timedelta,
        decoder_manifest_sync_id: Optional[str] = None,
        issued_time: Optional[datetime] = None,
    ):
        command_request_proto = self.proto_factory.create_actuator_command_request_proto(
            command_id, fqn, arg, arg_type, timeout, decoder_manifest_sync_id, issued_time
        )
        self.send_command_request_proto(command_request_proto)

    def send_last_known_state_command_request(
        self,
        command_id: str,
        state_template_sync_id: str,
        operation: LastKnownStateCommandOperation,
        deactivate_after_seconds: Optional[int] = None,
    ):
        command_request_proto = self.proto_factory.create_last_known_state_command_request_proto(
            command_id=command_id,
            state_template_sync_id=state_template_sync_id,
            operation=operation,
            deactivate_after_seconds=deactivate_after_seconds,
        )
        self.send_command_request_proto(command_request_proto)

    def send_state_templates(
        self,
        *,
        state_templates_to_add: Optional[List[StateTemplate]] = None,
        state_templates_to_remove: Optional[List[str]] = None,
        version: int = 1,
    ):
        state_templates_to_add = state_templates_to_add or []
        state_templates_to_remove = state_templates_to_remove or []
        state_templates_proto = self.proto_factory.create_state_templates_proto(
            state_templates_to_add=state_templates_to_add,
            state_templates_to_remove=state_templates_to_remove,
            version=version,
        )
        self.send_state_templates_proto(state_templates_proto)

    def send_state_templates_proto(
        self,
        last_known_state_proto: StateTemplates,
    ):
        if self.mqtt_client is None:
            raise RuntimeError("MQTT client not connected")

        outstring = last_known_state_proto.SerializeToString()
        with open(self.runpath / "state-template-list.bin", "wb") as write_file:
            write_file.write(outstring)
        with open(self.runpath / "state-template-list.json", "w") as write_file:
            write_file.write(MessageToJson(last_known_state_proto))
        self.add_metrics("SentStateTemplatesBytes", len(outstring), "Bytes")
        future = self.mqtt_client.publish(
            mqtt5.PublishPacket(
                topic=self.last_known_state_config_topic,
                payload=outstring,
                qos=mqtt5.QoS.AT_LEAST_ONCE,
            )
        )

        future.result()
        log.debug(
            "Sent LastKnownState collection scheme on topic "
            + self.last_known_state_config_topic
            + " with byte length: "
            + str(len(outstring))
        )

    def start_cyclic_can_messages(self):
        if self.high_load:
            dbc_file = self.script_path + "sample_high_bus_load.dbc"
        elif self.use_fd:
            dbc_file = self.script_path + "can_fd.dbc"
        else:
            dbc_file = self.script_path + "sample.dbc"
        if self.use_obd_broadcast:
            obd_config_file = self.script_path + "obd_config_sample_broadcast.json"
        elif self.use_extended_ids:
            obd_config_file = self.script_path + "obd_config_sample_29bit.json"
        else:
            obd_config_file = self.script_path + "obd_config_sample.json"

        self.canigen = Canigen(
            interface=self.local_can_interface,
            database_filename=dbc_file,
            obd_config_filename=obd_config_file,
            values_filename=self.script_path + "vals_sample_default.json",
            obd_answer_reverse_order=self.obd_answer_reverse_order,
        )

    def start_cyclic_ros_messages(self):
        if not self.ros2_enabled:
            raise RuntimeError("Can't start cyclic ROS2 messages. ROS2 is not enabled.")

        from rosigen import Rosigen

        os.environ["CYCLONEDDS_URI"] = f"file://{self.cyclonedds_config_file_path}"
        os.environ["RMW_IMPLEMENTATION"] = "rmw_cyclonedds_cpp"

        self.rosigen = Rosigen(
            config_filename=self.ros2_config_file,
            values_filename=self.ros2_vals_file,
        )

    def start_can_command_server(self):
        self.can_command_server = CanCommandServer(self.local_can_interface)

    def stop_cyclic_can_messages(self):
        """If cycling messages have been started, stop them"""
        if self.canigen is not None:
            self.canigen.stop()

    def stop_cyclic_ros_messages(self):
        if self.rosigen is not None:
            self.rosigen.stop()
            self.rosigen = None

        # TODO: When the setup is done via pytest fixtures, it is better to use pytest's
        # monkeypatch so that the previous value is automatically restored:
        # https://docs.pytest.org/en/latest/how-to/monkeypatch.html#monkeypatching-environment-variables
        if self.ros2_enabled:
            os.environ["CYCLONEDDS_URI"] = self.original_cyclonedds_config_file_uri
            os.environ["RMW_IMPLEMENTATION"] = self.original_ros2_rmw_implementation

    def stop_can_command_server(self):
        self.can_command_server.stop()

    def destroy_aws_resources(self):
        if self._delete_aws_thing_at_the_end:
            self._aws_thing.stop()

    def link_up(self):
        self.network_namespace.link_up()

    def link_down(self):
        self.network_namespace.link_down()

    def link_throttle(
        self,
        *,
        loss_percentage: Optional[int] = None,
        delay_ms: Optional[int] = None,
        rate_kbit: Optional[int] = None,
    ):
        self.network_namespace.configure_throttling(
            loss_percentage=loss_percentage, delay_ms=delay_ms, rate_kbit=rate_kbit
        )

    def set_can_signal(self, name, value):
        self.canigen.set_sig(name, value)

    def set_dtc(self, name, value):
        self.canigen.set_dtc(name, value)

    def set_obd_signal(self, name, value):
        self.canigen.set_pid(name, value)

    def verify_checkin_documents(self, document_set_desired: set):
        """Verifies the latest checkin contains the desired set of documents"""

        # Ensure that we have received at least one checkin
        assert len(self.received_checkins) > 0

        # Get the latest checkin
        latest_checkin = self.received_checkins[-1]

        # Create a set of the documents that were received in the checkin
        if latest_checkin.document_sync_ids:
            document_set_actual = set(latest_checkin.document_sync_ids)
        else:
            document_set_actual = set()

        log.info(f"Verifying {document_set_desired} is in the checkin: {document_set_actual}")

        # Ensure the sets contain the same documents
        assert document_set_actual == document_set_desired

        # Ensure the time of the checkin is after the start time of this test
        # The checkin timestamp is the system time, so any checking sent after faketime kicks
        # in will have a timestamp less than the start time we recorded.
        if not self.use_faketime:
            assert latest_checkin.timestamp_ms_epoch > (self.start_time.timestamp() * 1000)

    def verify_checkin_timing(self):
        """Verifies the checkin timing.

        It does that by calculating an average period of all checkins and comparing it to a
        target precision. Note that the timestamp we are using to measure is one generated by
        FWE, and is not affected by MQTT latency. Also note this is not a standalone test so
        that it can reuse the checkins sent in all the previous tests and save time on the
        system test
        """

        # The average would be skewed when using faketime as the checkin timestamp is the
        # system time.
        if self.use_faketime:
            log.info("Skipping checkin timing verification as it won't work well with faketime")
            return

        # With valgrind, thread activations can be delayed a lot, so we can't expect the checkin
        # intervals to be reliable.
        if self.is_valgrind_enabled():
            log.info("Skipping checkin timing verification as it won't work well with valgrind")
            return

        # Ensure we have at least 2 checkins
        assert len(self.received_checkins) > 1

        # Calculate the average time period between all received checkins
        time_delta = 0
        i = 0
        # Iterate over the checkins starting with the second element
        for checkin in self.received_checkins[1:]:
            i += 1
            time_delta += (
                checkin.timestamp_ms_epoch - self.received_checkins[i - 1].timestamp_ms_epoch
            )

        avg_time = time_delta / i

        # Test that the checkin time is within +- 1 second
        assert self.CHECKIN_PERIOD - 1000 < avg_time
        assert self.CHECKIN_PERIOD + 1000 > avg_time

    def get_results_from_s3(self, prefix: str, max_number_of_files: Optional[int] = None):
        log.info("Getting results from S3")
        s3_client = boto3.client("s3")
        result = s3_client.list_objects(Bucket=os.environ["TEST_S3_BUCKET_NAME"], Prefix=prefix)
        if "Contents" not in result:
            return {}

        ion_data = {}
        files = sorted(result["Contents"], key=lambda file: file["LastModified"])
        if max_number_of_files is None:
            max_number_of_files = len(files)

        s3_dir = self.runpath / prefix
        s3_dir.mkdir(parents=True, exist_ok=True)
        for file in files[:max_number_of_files]:
            results_file = s3_dir / os.path.basename(file["Key"])
            s3_client.download_file(
                os.environ["TEST_S3_BUCKET_NAME"], file["Key"], results_file.as_posix()
            )
            with open(results_file, "rb") as fp:
                ion_file_data = {
                    "prefix": prefix,
                    "data": simpleion.load(fp, single_value=False),
                }
                # Skip first element, which is metadata. For all the rest we replace the original
                # bytes() with a wrapper to limit the size in the test output in case of failure.
                for item in ion_file_data["data"][1:]:
                    item["signal_byte_values"] = ComplexSignalBytes(item["signal_byte_values"])
                ion_data[file["Key"]] = ion_file_data
        return ion_data

    def clean_up_s3_bucket(self, prefixes: List[str]):
        s3_client = boto3.client("s3")
        for prefix in prefixes:
            files = s3_client.list_objects(Bucket=os.environ["TEST_S3_BUCKET_NAME"], Prefix=prefix)
            if "Contents" in files:
                for file in files["Contents"]:
                    s3_client.delete_object(
                        Bucket=os.environ["TEST_S3_BUCKET_NAME"], Key=file["Key"]
                    )

    def get_reported_uploaded_s3_objects(self) -> Dict[str, str]:
        all_uploaded_s3_objects = {}
        for received_data in self.received_data:
            s3_objects = all_uploaded_s3_objects.get(received_data.campaign_sync_id, [])
            s3_objects.extend(received_data.uploaded_s3_objects)
            # Python 3.7+ dict is guaranteed to preserve insertion order, so use it instead of set
            # to remove duplicates while preserving receiving order.
            all_uploaded_s3_objects[received_data.campaign_sync_id] = list(
                dict.fromkeys(s3_objects)
            )
        return all_uploaded_s3_objects

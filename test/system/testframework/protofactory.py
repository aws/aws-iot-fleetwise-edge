#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import random
import re
import time
from dataclasses import dataclass
from datetime import datetime, timedelta
from enum import Enum
from typing import List, Optional, Union

import collection_schemes_pb2
import command_request_pb2
import common_types_pb2
import decoder_manifest_pb2
import pyparsing as pp
import state_templates_pb2
import vehicle_data_pb2
from google.protobuf.json_format import MessageToJson


class SignalType(Enum):
    BOOL = 1
    FLOAT32 = 2
    FLOAT64 = 3
    INT8 = 4
    INT16 = 5
    INT32 = 6
    INT64 = 7
    UINT8 = 8
    UINT16 = 9
    UINT32 = 10
    UINT64 = 11
    STRING = 12


@dataclass
class PeriodicUpdateStrategy:
    period_ms: int


@dataclass
class OnChangeUpdateStrategy:
    pass


@dataclass
class LastKnownStateSignalInformation:
    fqn: str


@dataclass
class StateTemplate:
    sync_id: str
    update_strategy: Union[PeriodicUpdateStrategy, OnChangeUpdateStrategy]
    signals: List[LastKnownStateSignalInformation]


class LastKnownStateCommandOperation(Enum):
    ACTIVATE = 1
    DEACTIVATE = 2
    FETCH_SNAPSHOT = 3


class ProtoFactory:
    NODE_OPERATOR = common_types_pb2.ConditionNode.NodeOperator.Operator
    OPERATORS = {
        "<": NODE_OPERATOR.COMPARE_SMALLER,
        ">": NODE_OPERATOR.COMPARE_BIGGER,
        "<=": NODE_OPERATOR.COMPARE_SMALLER_EQUAL,
        ">=": NODE_OPERATOR.COMPARE_BIGGER_EQUAL,
        "==": NODE_OPERATOR.COMPARE_EQUAL,
        "!=": NODE_OPERATOR.COMPARE_NOT_EQUAL,
        "&&": NODE_OPERATOR.LOGICAL_AND,
        "||": NODE_OPERATOR.LOGICAL_OR,
        "+": NODE_OPERATOR.ARITHMETIC_PLUS,
        "-": NODE_OPERATOR.ARITHMETIC_MINUS,
        "*": NODE_OPERATOR.ARITHMETIC_MULTIPLY,
        "/": NODE_OPERATOR.ARITHMETIC_DIVIDE,
    }

    NODE_WINDOW_FUNCTION = common_types_pb2.ConditionNode.NodeFunction.WindowFunction
    WINDOW_FUNCTIONS = {
        "last_window_min": NODE_WINDOW_FUNCTION.WindowType.LAST_WINDOW_MIN,
        "last_window_max": NODE_WINDOW_FUNCTION.WindowType.LAST_WINDOW_MAX,
        "last_window_avg": NODE_WINDOW_FUNCTION.WindowType.LAST_WINDOW_AVG,
        "prev_last_window_min": NODE_WINDOW_FUNCTION.WindowType.PREV_LAST_WINDOW_MIN,
        "prev_last_window_max": NODE_WINDOW_FUNCTION.WindowType.PREV_LAST_WINDOW_MAX,
        "prev_last_window_avg": NODE_WINDOW_FUNCTION.WindowType.PREV_LAST_WINDOW_AVG,
    }
    CUSTOM_FUNCTION = "custom_function"
    IS_NULL_FUNCTION = "isNull"

    VISION_SYSTEM_DATA_PRIMITIVE_TYPES = {
        "BOOL": decoder_manifest_pb2.PrimitiveType.BOOL,
        "FLOAT32": decoder_manifest_pb2.PrimitiveType.FLOAT32,
        "FLOAT64": decoder_manifest_pb2.PrimitiveType.FLOAT64,
        "INT8": decoder_manifest_pb2.PrimitiveType.INT8,
        "INT16": decoder_manifest_pb2.PrimitiveType.INT16,
        "INT32": decoder_manifest_pb2.PrimitiveType.INT32,
        "INT64": decoder_manifest_pb2.PrimitiveType.INT64,
        "UINT8": decoder_manifest_pb2.PrimitiveType.UINT8,
        "UINT16": decoder_manifest_pb2.PrimitiveType.UINT16,
        "UINT32": decoder_manifest_pb2.PrimitiveType.UINT32,
        "UINT64": decoder_manifest_pb2.PrimitiveType.UINT64,
        "BYTE": decoder_manifest_pb2.PrimitiveType.UINT8,
        "CHAR": decoder_manifest_pb2.PrimitiveType.UINT8,
    }

    VISION_SYSTEM_DATA_STRING_TYPES = {
        "STRING": decoder_manifest_pb2.StringEncoding.UTF_8,
        "WSTRING": decoder_manifest_pb2.StringEncoding.UTF_16,
    }

    def __init__(
        self,
        *,
        can_dbc_file=None,
        obd_json_file=None,
        guess_signal_types=False,
        number_of_can_channels=1,
        vision_system_data_json_file=None,
        custom_decoder_json_files=None,
        decoder_manifest_sync_id="decoder_manifest_1",
        node_json_files=None,
    ):
        custom_decoder_json_files = custom_decoder_json_files or []
        node_json_files = node_json_files or []
        pp.ParserElement.enablePackrat()
        self.decoder_manifest_sync_id = decoder_manifest_sync_id
        self.guess_signal_types = guess_signal_types
        self.create_decoder_manifest(
            can_dbc_file,
            obd_json_file,
            number_of_can_channels,
            vision_system_data_json_file,
            custom_decoder_json_files,
            node_json_files,
        )

    def create_decoder_manifest(
        self,
        can_dbc_file=None,
        obd_json_file=None,
        number_of_can_channels=1,
        vision_system_data_json_file=None,
        custom_decoder_json_files=None,
        node_json_files=None,
    ):
        custom_decoder_json_files = custom_decoder_json_files or []
        node_json_files = node_json_files or []
        self.signal_name_to_type = {}
        for f in node_json_files:
            with open(f) as fp:
                nodes = json.load(fp)
                for node in nodes:
                    if "sensor" in node:
                        self.signal_name_to_type[node["sensor"]["fullyQualifiedName"]] = node[
                            "sensor"
                        ]["dataType"]
                    elif "actuator" in node:
                        self.signal_name_to_type[node["actuator"]["fullyQualifiedName"]] = node[
                            "actuator"
                        ]["dataType"]

        self.signal_name_to_id = {}
        self.decoder_manifest_proto = decoder_manifest_pb2.DecoderManifest()
        self.decoder_manifest_proto.sync_id = self.decoder_manifest_sync_id
        signal_id = 1
        if can_dbc_file:
            import cantools

            db = cantools.database.load_file(can_dbc_file)
            for channel_id in range(1, number_of_can_channels + 1):
                for message in db.messages:
                    for signal in message.signals:
                        can_signal = self.decoder_manifest_proto.can_signals.add()

                        can_signal.signal_id = signal_id
                        can_signal.interface_id = str(channel_id)
                        can_signal.message_id = message.frame_id

                        can_signal.is_big_endian = signal.byte_order == "big_endian"
                        can_signal.is_signed = signal.is_signed
                        if can_signal.is_big_endian:
                            pos = 7 - (signal.start % 8) + (signal.length - 1)
                            if pos < 8:
                                can_signal.start_bit = signal.start - signal.length + 1
                            else:
                                byte_count = int(pos / 8)
                                can_signal.start_bit = int(
                                    7 - (pos % 8) + (byte_count * 8) + int(signal.start / 8) * 8
                                )
                        else:
                            can_signal.start_bit = signal.start
                        can_signal.offset = signal.offset
                        can_signal.factor = signal.scale
                        can_signal.length = signal.length
                        can_signal.signal_value_type = (
                            decoder_manifest_pb2.FLOATING_POINT
                            if signal.is_float
                            else decoder_manifest_pb2.INTEGER
                        )

                        if self.guess_signal_types:
                            signal_type = self._get_signal_type(
                                scale=signal.scale,
                                offset=signal.offset,
                                length=signal.length,
                                raw_data_is_signed=signal.is_signed,
                                raw_data_is_float=signal.is_float,
                            )
                            # Don't set the signal type when it is double since this should be the
                            # default when the type is not present in the decoder manifest.
                            if signal_type != decoder_manifest_pb2.FLOAT64:
                                can_signal.primitive_type = signal_type
                        else:
                            can_signal.primitive_type = self._get_signal_primitive_type(signal.name)
                        signal_name = signal.name
                        if channel_id > 1:
                            signal_name += "_channel_" + str(channel_id)
                        # print(can_signal.signal_id, signal_name)
                        self.signal_name_to_id[signal_name] = signal_id
                        signal_id += 50000

        if obd_json_file:
            with open(obd_json_file) as f:
                obd_json = json.load(f)

            for signal in obd_json["obd_pid_signals"]:
                obd_pid_node = self.decoder_manifest_proto.obd_pid_signals.add()
                obd_pid_node.signal_id = signal_id
                obd_pid_node.pid_response_length = signal["pid_response_length"]
                obd_pid_node.service_mode = signal["service_mode"]
                obd_pid_node.pid = int(signal["pid"], 16)
                obd_pid_node.scaling = signal["scaling"]
                obd_pid_node.offset = signal["offset"]
                obd_pid_node.start_byte = signal["start_byte"]
                obd_pid_node.byte_length = signal["byte_length"]
                obd_pid_node.bit_right_shift = signal["bit_right_shift"]
                obd_pid_node.bit_mask_length = signal["bit_mask_length"]
                if "is_signed" in signal:
                    obd_pid_node.is_signed = signal["is_signed"]
                if "signal_value_type" in signal:
                    signal_value_type = signal["signal_value_type"]
                    assert signal_value_type in ["INTEGER", "FLOATING_POINT"]
                    obd_pid_node.signal_value_type = (
                        decoder_manifest_pb2.FLOATING_POINT
                        if signal_value_type == "FLOATING_POINT"
                        else decoder_manifest_pb2.INTEGER
                    )
                if self.guess_signal_types:
                    signal_type = self._get_signal_type(
                        scale=signal["scaling"],
                        offset=signal["offset"],
                        length=signal["byte_length"] * 8,
                        raw_data_is_signed=obd_pid_node.is_signed,
                        raw_data_is_float=obd_pid_node.signal_value_type
                        == decoder_manifest_pb2.FLOATING_POINT,
                    )
                    # Don't set the signal type when it is double since this should be the
                    # default when the type is not present in the decoder manifest.
                    if signal_type != decoder_manifest_pb2.FLOAT64:
                        obd_pid_node.primitive_type = signal_type
                else:
                    obd_pid_node.primitive_type = self._get_signal_primitive_type(
                        signal["obd_signal_name"]
                    )
                self.signal_name_to_id[signal["obd_signal_name"]] = signal_id
                signal_id += 1

        self.vision_system_data_model = []
        if vision_system_data_json_file:
            with open(vision_system_data_json_file) as fp:
                self.vision_system_data_model = json.loads(fp.read())
            for vision_system_data_message in self.vision_system_data_model:
                complex_signal = self.decoder_manifest_proto.complex_signals.add()
                complex_signal.signal_id = signal_id
                complex_signal.interface_id = vision_system_data_message["interfaceId"]
                complex_signal.message_id = vision_system_data_message["messageSignal"]["topicName"]
                complex_signal.root_type_id = self.add_vision_system_data_message(
                    vision_system_data_message["messageSignal"]["structuredMessage"],
                )
                self.signal_name_to_id[vision_system_data_message["fullyQualifiedName"]] = signal_id
                signal_id += 1

        for f in custom_decoder_json_files:
            with open(f) as fp:
                custom_decoders = json.loads(fp.read())
            for decoder in custom_decoders:
                custom_signal = self.decoder_manifest_proto.custom_decoding_signals.add()
                custom_signal.signal_id = signal_id
                custom_signal.interface_id = decoder["interfaceId"]
                custom_signal.custom_decoding_id = decoder["customDecodingSignal"]["id"]
                self.signal_name_to_id[decoder["fullyQualifiedName"]] = signal_id
                custom_signal.primitive_type = self._get_signal_primitive_type(
                    decoder["fullyQualifiedName"]
                )
                signal_id += 1

    def _get_signal_primitive_type(self, signal_name):
        if signal_name not in self.signal_name_to_type:  # No type information defined for signal
            return decoder_manifest_pb2.NULL  # FWE defaults to FLOAT64
        data_type = self.signal_name_to_type[signal_name]
        if data_type == "BOOLEAN":
            return decoder_manifest_pb2.BOOL
        elif data_type == "UINT8":
            return decoder_manifest_pb2.UINT8
        elif data_type == "UINT16":
            return decoder_manifest_pb2.UINT16
        elif data_type == "UINT32":
            return decoder_manifest_pb2.UINT32
        elif data_type == "UINT64":
            return decoder_manifest_pb2.UINT64
        elif data_type == "INT8":
            return decoder_manifest_pb2.INT8
        elif data_type == "INT16":
            return decoder_manifest_pb2.INT16
        elif data_type == "INT32":
            return decoder_manifest_pb2.INT32
        elif data_type == "INT64":
            return decoder_manifest_pb2.INT64
        elif data_type == "FLOAT":
            return decoder_manifest_pb2.FLOAT32
        elif data_type == "DOUBLE":
            return decoder_manifest_pb2.FLOAT64
        elif data_type == "STRING":
            return decoder_manifest_pb2.STRING
        else:
            raise Exception(f"Unknown data type {data_type}")

    def _get_signal_type(self, scale, offset, length, raw_data_is_signed, raw_data_is_float):
        if raw_data_is_float:
            if length == 32:
                return decoder_manifest_pb2.FLOAT32
            elif length == 64:
                return decoder_manifest_pb2.FLOAT64
            else:
                raise ValueError(
                    f"Invalid raw floating point length. Expected 32 or 64 bits. {length=}"
                )

        is_scaled_value_integer = int(offset) == offset and int(scale) == scale
        if not is_scaled_value_integer:
            return decoder_manifest_pb2.FLOAT64

        if length == 1 and scale == 1 and offset == 0:
            return decoder_manifest_pb2.BOOL

        # We can't consider just raw_data_is_signed for determining whether the type is signed or
        # unsigned.
        # The type needs to be appropriate for storing the final value after considering scale and
        # offset, so we need to calculate the minimum and maximum.
        if raw_data_is_signed:
            minimum = -pow(2, (length - 1))
            minimum = (minimum * scale) + offset
            maximum = pow(2, (length - 1)) - 1
            maximum = (maximum * scale) + offset
        else:
            minimum = 0 + offset
            maximum = pow(2, length) - 1
            maximum = (maximum * scale) + offset

        is_signed = minimum < 0
        if is_signed:
            if minimum >= -128 and maximum <= 127:
                return decoder_manifest_pb2.INT8
            elif minimum >= -32768 and maximum <= 32767:
                return decoder_manifest_pb2.INT16
            elif minimum >= -2147483648 and maximum <= 2147483647:
                return decoder_manifest_pb2.INT32
            else:
                return decoder_manifest_pb2.INT64
        else:
            if maximum <= 255:
                return decoder_manifest_pb2.UINT8
            elif maximum <= 65535:
                return decoder_manifest_pb2.UINT16
            elif maximum <= 4294967295:
                return decoder_manifest_pb2.UINT32
            else:
                return decoder_manifest_pb2.UINT64

    def add_vision_system_data_message(self, msg):
        if "primitiveMessageDefinition" in msg:
            primitive_type = msg["primitiveMessageDefinition"]["ros2PrimitiveMessageDefinition"][
                "primitiveType"
            ]
            if primitive_type in self.VISION_SYSTEM_DATA_STRING_TYPES:
                # Search for existing type:
                for complex_type in self.decoder_manifest_proto.complex_types:
                    # TODO Model currently doesn't support fixed or maximum length strings
                    if (
                        complex_type.HasField("string_data")
                        and complex_type.string_data.encoding
                        == self.VISION_SYSTEM_DATA_STRING_TYPES[primitive_type]
                        and complex_type.string_data.size == 0
                    ):
                        break
                else:
                    complex_type = self.decoder_manifest_proto.complex_types.add()
                    complex_type.type_id = len(self.decoder_manifest_proto.complex_types)
                    complex_type.string_data.encoding = self.VISION_SYSTEM_DATA_STRING_TYPES[
                        primitive_type
                    ]
                    # TODO Model currently doesn't support fixed or maximum length strings
                    complex_type.string_data.size = 0
            elif primitive_type in self.VISION_SYSTEM_DATA_PRIMITIVE_TYPES:
                scaling = msg["primitiveMessageDefinition"]["ros2PrimitiveMessageDefinition"].get(
                    "scaling", 1.0
                )
                offset = msg["primitiveMessageDefinition"]["ros2PrimitiveMessageDefinition"].get(
                    "offset", 0.0
                )
                # Search for existing type:
                for complex_type in self.decoder_manifest_proto.complex_types:
                    if (
                        complex_type.HasField("primitive_data")
                        and complex_type.primitive_data.primitive_type
                        == self.VISION_SYSTEM_DATA_PRIMITIVE_TYPES[primitive_type]
                        and complex_type.primitive_data.scaling == scaling
                        and complex_type.primitive_data.offset == offset
                    ):
                        break
                else:
                    complex_type = self.decoder_manifest_proto.complex_types.add()
                    complex_type.type_id = len(self.decoder_manifest_proto.complex_types)
                    complex_type.primitive_data.primitive_type = (
                        self.VISION_SYSTEM_DATA_PRIMITIVE_TYPES[primitive_type]
                    )
                    complex_type.primitive_data.scaling = scaling
                    complex_type.primitive_data.offset = offset
            else:
                raise Exception("Unknown primitive type " + primitive_type)
        elif "structuredMessageListDefinition" in msg:
            list_length = 0  # 0 for DYNAMIC_UNBOUNDED_CAPACITY
            if msg["structuredMessageListDefinition"]["listType"] == "FIXED_CAPACITY":
                list_length = msg["structuredMessageListDefinition"]["capacity"]
            elif msg["structuredMessageListDefinition"]["listType"] == "DYNAMIC_BOUNDED_CAPACITY":
                list_length = (
                    msg["structuredMessageListDefinition"]["capacity"] * -1
                )  # see decoder_manifest.proto ComplexArray
            type_id = self.add_vision_system_data_message(
                msg["structuredMessageListDefinition"]["memberType"],
            )
            # Search for existing type:
            for complex_type in self.decoder_manifest_proto.complex_types:
                if (
                    complex_type.HasField("array")
                    and complex_type.array.size == list_length
                    and complex_type.array.type_id == type_id
                ):
                    break
            else:
                complex_type = self.decoder_manifest_proto.complex_types.add()
                complex_type.type_id = len(self.decoder_manifest_proto.complex_types)
                complex_type.array.size = list_length
                complex_type.array.type_id = type_id
        elif "structuredMessageDefinition" in msg:
            type_ids = []
            for member in msg["structuredMessageDefinition"]:
                type_ids.append(
                    self.add_vision_system_data_message(
                        member["dataType"],
                    )
                )
            # Search for existing type:
            for complex_type in self.decoder_manifest_proto.complex_types:
                if complex_type.HasField("struct") and len(complex_type.struct.members) == len(
                    type_ids
                ):
                    for i, member in enumerate(complex_type.struct.members):
                        if member.type_id != type_ids[i]:
                            break
                    else:
                        break
            else:
                complex_type = self.decoder_manifest_proto.complex_types.add()
                complex_type.type_id = len(self.decoder_manifest_proto.complex_types)
                for type_id in type_ids:
                    member = complex_type.struct.members.add()
                    member.type_id = type_id
        else:
            raise Exception("Unknown message type")
        return complex_type.type_id

    def get_signal_id_and_path(self, fqn):
        for message in self.vision_system_data_model:
            if fqn.startswith(message["fullyQualifiedName"]):
                signal_id = self.signal_name_to_id[message["fullyQualifiedName"]]
                signal_path = []
                path = fqn[len(message["fullyQualifiedName"]) :]
                if path != "":
                    path = re.split(r"\.|\[", path.strip("."))
                    message = message["messageSignal"]["structuredMessage"]
                    for subpath in path:
                        if "structuredMessageDefinition" in message:
                            member_index = 0
                            for member in message["structuredMessageDefinition"]:
                                if subpath == member["fieldName"]:
                                    signal_path += [member_index]
                                    message = member["dataType"]
                                    break
                                member_index += 1
                            else:
                                raise Exception("Unknown field " + subpath)
                        elif "structuredMessageListDefinition" in message:
                            member_index = int(subpath.replace("]", ""))
                            length = message["structuredMessageListDefinition"]["capacity"]
                            if length != 0 and member_index >= abs(length):
                                raise Exception(
                                    f"Index {member_index} out of range for list size {abs(length)}"
                                )
                            signal_path += [member_index]
                            message = message["structuredMessageListDefinition"]["memberType"]
                        elif "primitiveMessageDefinition" in message:
                            message_type = message["primitiveMessageDefinition"][
                                "ros2PrimitiveMessageDefinition"
                            ]["primitiveType"]
                            if message_type not in ["STRING", "WSTRING"]:
                                raise Exception("Invalid signal path for primitive type " + fqn)
                            member_index = int(subpath.replace("]", ""))
                            signal_path += [member_index]
                            message = {}
                        else:
                            raise Exception("Invalid signal path " + fqn)
                return (signal_id, signal_path)
        if fqn in self.signal_name_to_id:
            return (self.signal_name_to_id[fqn], None)
        raise Exception("Invalid signal path " + fqn)

    def get_fqn(self, signal_id):
        for fqn, i in self.signal_name_to_id.items():
            if signal_id == i:
                return fqn
        raise Exception(f"Invalid signal id {signal_id}")

    def parse_expression(self, expression):
        # Remove $variable.`` wrappers, this module directly uses fully-qualified names
        expression = re.sub(r"\$variable\.`(.+?)`", r"\1", expression)

        number = pp.pyparsing_common.number()
        single_quoted_string = pp.QuotedString(quoteChar="'", unquoteResults=False)
        double_quoted_string = pp.QuotedString(quoteChar='"', unquoteResults=False)
        variable = pp.Word(pp.alphas, pp.alphanums + "_-.[]/")
        term = variable | number | single_quoted_string | double_quoted_string

        function_list = " ".join(
            list(self.WINDOW_FUNCTIONS.keys()) + [self.CUSTOM_FUNCTION, self.IS_NULL_FUNCTION]
        )
        funcop = pp.oneOf(function_list)
        multop = pp.oneOf("* /")
        plusop = pp.oneOf("+ -")
        compop = pp.oneOf(">= <= > < == !=")
        commaop = pp.Literal(",").suppress()
        parser = pp.infixNotation(
            term,
            [
                (funcop, 1, pp.opAssoc.RIGHT),
                (multop, 2, pp.opAssoc.LEFT),
                (plusop, 2, pp.opAssoc.LEFT),
                (compop, 2, pp.opAssoc.LEFT),
                (
                    "!",
                    1,
                    pp.opAssoc.RIGHT,
                ),
                (
                    "&&",
                    2,
                    pp.opAssoc.LEFT,
                ),
                (
                    "||",
                    2,
                    pp.opAssoc.LEFT,
                ),
                (commaop, 2, pp.opAssoc.LEFT),
            ],
        )
        return parser.parseString(expression, parseAll=True)[0]

    def get_trigger_mode(self, mode_in: str):
        condition_trigger_mode = (
            collection_schemes_pb2.ConditionBasedCollectionScheme.ConditionTriggerMode
        )
        trigger_mode = {
            "RISING_EDGE": condition_trigger_mode.TRIGGER_ONLY_ON_RISING_EDGE,
            "ALWAYS": condition_trigger_mode.TRIGGER_ALWAYS,
        }
        if mode_in not in trigger_mode:
            raise Exception(
                f"ERROR : The trigger mode {mode_in} is not supported."
                f" Please choose from {trigger_mode.keys()}"
            )
        return trigger_mode[mode_in]

    def ensure_signal_in_signals_to_collect(
        self, name, collection_scheme_config, collection_scheme
    ):
        if "signalsToCollect" in collection_scheme_config:
            for sig in collection_scheme_config["signalsToCollect"]:
                if sig["name"] == name:
                    return
        collected_signal = collection_scheme.signal_information.add()
        signal_id, signal_path = self.get_signal_id_and_path(name)
        collected_signal.signal_id = signal_id
        if signal_path:
            for i in signal_path:
                collected_signal.signal_path.signal_path.append(i)
        collected_signal.sample_buffer_size = 750
        collected_signal.minimum_sample_period_ms = 0
        collected_signal.fixed_window_period_ms = 0
        collected_signal.condition_only_signal = True

    def is_quoted_string(self, s):
        return isinstance(s, str) and len(s) >= 2 and s[0] in ["'", '"'] and s[-1] in ["'", '"']

    def unescape_string(self, s):
        return s.encode().decode("unicode-escape")

    def add_condition(self, condition, collection_scheme_config, collection_scheme, node):
        if isinstance(condition, str):
            if self.is_quoted_string(condition):  # quoted string is string literal
                node.node_string_value = self.unescape_string(condition[1:-1])
            elif condition == "true":  # Boolean true
                node.node_boolean_value = True
            elif condition == "false":  # Boolean false
                node.node_boolean_value = False
            else:  # unquoted string is signal name
                signal_id, signal_path = self.get_signal_id_and_path(condition)
                if signal_path is not None:
                    node.node_primitive_type_in_signal.signal_id = signal_id
                    for i in signal_path:
                        node.node_primitive_type_in_signal.signal_path.signal_path.append(i)
                else:
                    node.node_signal_id = signal_id
                self.ensure_signal_in_signals_to_collect(
                    condition, collection_scheme_config, collection_scheme
                )
        elif isinstance(condition, float) or isinstance(condition, int):  # numeric literal
            node.node_double_value = condition
        elif condition[0] == "!":
            node.node_operator.operator = self.NODE_OPERATOR.LOGICAL_NOT
            self.add_condition(
                condition[1],
                collection_scheme_config,
                collection_scheme,
                node.node_operator.left_child,
            )
        elif len(condition) >= 3 and condition[1] in self.OPERATORS:
            # Throw an error if there are more than 2 arguments in one bracket pair
            if len(condition) > 3:
                raise Exception("Too many arguments to operator " + condition[1])
            node.node_operator.operator = self.OPERATORS[condition[1]]
            self.add_condition(
                condition[0],
                collection_scheme_config,
                collection_scheme,
                node.node_operator.left_child,
            )
            self.add_condition(
                condition[2],
                collection_scheme_config,
                collection_scheme,
                node.node_operator.right_child,
            )
        elif condition[0] in self.WINDOW_FUNCTIONS:
            window_function = node.node_function.window_function
            window_function.window_type = self.WINDOW_FUNCTIONS[condition[0]]
            signal_id, signal_path = self.get_signal_id_and_path(condition[1])
            if signal_path is not None:
                primitive_type_in_signal = window_function.primitive_type_in_signal
                primitive_type_in_signal.signal_id = signal_id
                for i in signal_path:
                    primitive_type_in_signal.signal_path.signal_path.append(i)
            else:
                window_function.signal_id = signal_id
            self.ensure_signal_in_signals_to_collect(
                condition[1], collection_scheme_config, collection_scheme
            )
        elif condition[0] == self.CUSTOM_FUNCTION:
            if len(condition) < 2:
                raise Exception("No name provided to custom_function")
            if not self.is_quoted_string(condition[1][0]):
                raise Exception("Expected first argument to custom_function to be name")
            node.node_function.custom_function.function_name = self.unescape_string(
                condition[1][0][1:-1]
            )
            for arg in condition[1][1:]:
                custom_function_arg = node.node_function.custom_function.params.add()
                self.add_condition(
                    arg,
                    collection_scheme_config,
                    collection_scheme,
                    custom_function_arg,
                )
        elif condition[0] == self.IS_NULL_FUNCTION:
            if len(condition) != 2:
                raise Exception("One argument expected to isNull")
            self.add_condition(
                condition[1],
                collection_scheme_config,
                collection_scheme,
                node.node_function.is_null_function.expression,
            )
        else:
            raise Exception("Error: expression syntax error")

    def create_collection_schemes_proto(self, collection_schemes_config):
        self.collection_schemes_proto = collection_schemes_pb2.CollectionSchemes()
        self.collection_schemes_proto.timestamp_ms_epoch = round(time.time() * 1000)
        for collection_scheme_config in collection_schemes_config:
            self.create_collection_scheme_proto(collection_scheme_config)

    def create_collection_scheme_proto(self, collection_scheme_config):
        cs = self.collection_schemes_proto.collection_schemes.add()

        cs.campaign_sync_id = collection_scheme_config.get(
            "campaignSyncId", "dummyCampaignArn-" + str(random.getrandbits(16))
        )

        cs.campaign_arn = collection_scheme_config.get(
            "campaignArn", "dummyCampaignArn-" + str(random.getrandbits(16))
        )

        cs.decoder_manifest_sync_id = self.decoder_manifest_sync_id

        start_time = collection_scheme_config.get("startTime", datetime(year=2021, month=5, day=12))
        cs.start_time_ms_epoch = int(start_time.timestamp() * 1000)
        # ~ Sometime in the future
        # when warp speed has been reached and this collectionScheme hasn't expired yet
        expiry_time = collection_scheme_config.get(
            "expiryTime", datetime(year=2053, month=1, day=19)
        )
        cs.expiry_time_ms_epoch = int(expiry_time.timestamp() * 1000)

        cs.persist_all_collected_data = (
            collection_scheme_config.get("spoolingMode", "OFF") == "TO_DISK"
        )

        cs.compress_collected_data = collection_scheme_config.get("compression", "OFF") == "SNAPPY"

        cs.priority = collection_scheme_config.get("priority", 0)

        cs.after_duration_ms = collection_scheme_config.get("postTriggerCollectionDuration", 0)

        cs.include_active_dtcs = (
            collection_scheme_config.get("diagnosticsMode", "OFF") == "SEND_ACTIVE_DTCS"
        )

        if "timeBasedCollectionScheme" in collection_scheme_config["collectionScheme"]:
            cs.time_based_collection_scheme.time_based_collection_scheme_period_ms = (
                collection_scheme_config["collectionScheme"]["timeBasedCollectionScheme"][
                    "periodMs"
                ]
            )
        elif "conditionBasedCollectionScheme" in collection_scheme_config["collectionScheme"]:
            cs.condition_based_collection_scheme.condition_minimum_interval_ms = (
                collection_scheme_config["collectionScheme"]["conditionBasedCollectionScheme"].get(
                    "minimumTriggerIntervalMs", 0
                )
            )

            cs.condition_based_collection_scheme.condition_language_version = (
                collection_scheme_config["collectionScheme"]["conditionBasedCollectionScheme"].get(
                    "conditionLanguageVersion", 0
                )
            )

            cs.condition_based_collection_scheme.condition_trigger_mode = self.get_trigger_mode(
                collection_scheme_config["collectionScheme"]["conditionBasedCollectionScheme"].get(
                    "triggerMode", "ALWAYS"
                )
            )

            self.add_condition(
                self.parse_expression(
                    collection_scheme_config["collectionScheme"]["conditionBasedCollectionScheme"][
                        "expression"
                    ]
                ),
                collection_scheme_config,
                cs,
                cs.condition_based_collection_scheme.condition_tree,
            )
        else:
            raise Exception("Unsupported collection scheme type")

        if "signalsToCollect" in collection_scheme_config:
            for sig in collection_scheme_config["signalsToCollect"]:
                collected_signal = cs.signal_information.add()
                signal_id, signal_path = self.get_signal_id_and_path(sig["name"])
                collected_signal.signal_id = signal_id
                if signal_path:
                    for i in signal_path:
                        collected_signal.signal_path.signal_path.append(i)
                collected_signal.sample_buffer_size = sig.get("maxSampleCount", 1000)
                collected_signal.minimum_sample_period_ms = sig.get("minimumSamplingIntervalMs", 0)
                collected_signal.fixed_window_period_ms = 1000
                collected_signal.condition_only_signal = False
                if sig.get("dataPartitionId"):
                    collected_signal.data_partition_id = sig.get("dataPartitionId")

        if "s3UploadMetadata" in collection_scheme_config:
            cs.s3_upload_metadata.bucket_name = collection_scheme_config["s3UploadMetadata"][
                "bucketName"
            ]
            cs.s3_upload_metadata.bucket_owner_account_id = collection_scheme_config[
                "s3UploadMetadata"
            ]["bucketOwner"]
            cs.s3_upload_metadata.prefix = collection_scheme_config["s3UploadMetadata"]["prefix"]
            cs.s3_upload_metadata.region = collection_scheme_config["s3UploadMetadata"]["region"]

        if "storeAndForwardConfiguration" in collection_scheme_config:
            for partition in collection_scheme_config["storeAndForwardConfiguration"]:
                sf_partition = cs.store_and_forward_configuration.partition_configuration.add()
                sf_partition.storage_options.maximum_size_in_bytes = partition["storageOptions"][
                    "maximumSizeInBytes"
                ]
                sf_partition.storage_options.storage_location = partition["storageOptions"][
                    "storageLocation"
                ]
                sf_partition.storage_options.minimum_time_to_live_in_seconds = partition[
                    "storageOptions"
                ]["minimumTimeToLiveInSeconds"]
                self.add_condition(
                    self.parse_expression(partition["uploadOptions"]["conditionTree"]),
                    collection_scheme_config,
                    cs,
                    sf_partition.upload_options.condition_tree,
                )

        if "signalsToFetch" in collection_scheme_config:
            for fetch_info in collection_scheme_config["signalsToFetch"]:
                signal_fetch_info = cs.signal_fetch_information.add()
                signal_id, signal_path = self.get_signal_id_and_path(
                    fetch_info["fullyQualifiedName"]
                )
                signal_fetch_info.signal_id = signal_id
                signal_fetch_info.condition_language_version = 1

                for action_expression in fetch_info["actions"]:
                    action_condition = signal_fetch_info.actions.add()
                    self.add_condition(
                        self.parse_expression(action_expression),
                        collection_scheme_config,
                        cs,
                        action_condition,
                    )

                if "timeBased" in fetch_info["signalFetchConfig"]:
                    signal_fetch_info.time_based.reset_max_execution_count_interval_ms = 0
                    signal_fetch_info.time_based.max_execution_count = 0

                    signal_fetch_info.time_based.execution_frequency_ms = fetch_info[
                        "signalFetchConfig"
                    ]["timeBased"]["executionFrequencyMs"]
                elif "conditionBased" in fetch_info["signalFetchConfig"]:
                    signal_fetch_info.condition_based.condition_trigger_mode = (
                        self.get_trigger_mode(
                            fetch_info["signalFetchConfig"]["conditionBased"].get(
                                "triggerMode", "ALWAYS"
                            )
                        )
                    )
                    self.add_condition(
                        self.parse_expression(
                            fetch_info["signalFetchConfig"]["conditionBased"]["conditionExpression"]
                        ),
                        collection_scheme_config,
                        cs,
                        signal_fetch_info.condition_based.condition_tree,
                    )
                else:
                    raise Exception("Unsupported fetch config")

    def create_actuator_command_request_proto(
        self,
        command_id: str,
        fqn: str,
        arg,
        arg_type: SignalType,
        timeout: timedelta,
        decoder_manifest_sync_id: Optional[str] = None,
        issued_time: Optional[datetime] = None,
    ):
        issued_time = issued_time or datetime.now()
        command_request = command_request_pb2.CommandRequest()
        command_request.command_id = command_id
        command_request.timeout_ms = int(timeout.total_seconds() * 1000)
        actuator_command = command_request_pb2.ActuatorCommand()
        actuator_command.signal_id = self.signal_name_to_id[fqn]
        if arg_type == SignalType.BOOL:
            actuator_command.boolean_value = arg
        elif arg_type == SignalType.FLOAT32:
            actuator_command.float_value = arg
        elif arg_type == SignalType.FLOAT64:
            actuator_command.double_value = arg
        elif arg_type == SignalType.INT8:
            actuator_command.int8_value = arg
        elif arg_type == SignalType.INT16:
            actuator_command.int16_value = arg
        elif arg_type == SignalType.INT32:
            actuator_command.int32_value = arg
        elif arg_type == SignalType.INT64:
            actuator_command.int64_value = arg
        elif arg_type == SignalType.UINT8:
            actuator_command.uint8_value = arg
        elif arg_type == SignalType.UINT16:
            actuator_command.uint16_value = arg
        elif arg_type == SignalType.UINT32:
            actuator_command.uint32_value = arg
        elif arg_type == SignalType.UINT64:
            actuator_command.uint64_value = arg
        elif arg_type == SignalType.STRING:
            actuator_command.string_value = arg
        else:
            raise Exception("Unknown arg type: " + str(arg_type))
        actuator_command.decoder_manifest_sync_id = (
            decoder_manifest_sync_id if decoder_manifest_sync_id else self.decoder_manifest_sync_id
        )
        command_request.issued_timestamp_ms = int(issued_time.timestamp() * 1000)
        command_request.actuator_command.CopyFrom(actuator_command)
        return command_request

    def create_last_known_state_command_request_proto(
        self,
        command_id: str,
        state_template_sync_id: str,
        operation: LastKnownStateCommandOperation,
        deactivate_after_seconds: Optional[int] = None,
    ):
        command_request = command_request_pb2.CommandRequest()
        command_request.command_id = command_id
        lks_command = command_request_pb2.LastKnownStateCommand()
        state_template_information = (
            command_request_pb2.LastKnownStateCommand.StateTemplateInformation()
        )
        state_template_information.state_template_sync_id = state_template_sync_id
        if operation == LastKnownStateCommandOperation.ACTIVATE:
            activate_operation = command_request_pb2.LastKnownStateCommand.ActivateOperation()
            if deactivate_after_seconds is not None:
                activate_operation.deactivate_after_seconds = deactivate_after_seconds
            state_template_information.activate_operation.CopyFrom(activate_operation)
        elif operation == LastKnownStateCommandOperation.DEACTIVATE:
            state_template_information.deactivate_operation.CopyFrom(
                command_request_pb2.LastKnownStateCommand.DeactivateOperation()
            )
        elif operation == LastKnownStateCommandOperation.FETCH_SNAPSHOT:
            state_template_information.fetch_snapshot_operation.CopyFrom(
                command_request_pb2.LastKnownStateCommand.FetchSnapshotOperation()
            )
        else:
            raise RuntimeError(f"Unknown LastKnownState operation: {operation}")

        lks_command.state_template_information.append(state_template_information)

        command_request.last_known_state_command.CopyFrom(lks_command)

        return command_request

    def create_state_templates_proto(
        self,
        *,
        state_templates_to_add: List[StateTemplate],
        state_templates_to_remove: List[str],
        version: int,
    ) -> state_templates_pb2.StateTemplates:
        state_templates_proto = state_templates_pb2.StateTemplates()

        state_templates_proto.version = version
        state_templates_proto.decoder_manifest_sync_id = self.decoder_manifest_sync_id

        for state_template in state_templates_to_add:
            state_template_information = state_templates_proto.state_templates_to_add.add()
            state_template_information.state_template_sync_id = state_template.sync_id

            if isinstance(state_template.update_strategy, PeriodicUpdateStrategy):
                periodic_update_strategy = state_templates_pb2.PeriodicUpdateStrategy()
                periodic_update_strategy.period_ms = state_template.update_strategy.period_ms
                state_template_information.periodic_update_strategy.CopyFrom(
                    periodic_update_strategy
                )
            elif isinstance(state_template.update_strategy, OnChangeUpdateStrategy):
                state_template_information.on_change_update_strategy.CopyFrom(
                    state_templates_pb2.OnChangeUpdateStrategy()
                )
            else:
                raise RuntimeError(f"Unknown update strategy: {state_template.update_strategy}")

            for signal in state_template.signals:
                state_template_information.signal_ids.append(self.signal_name_to_id[signal.fqn])

        for state_template_sync_id in state_templates_to_remove:
            state_templates_proto.state_template_sync_ids_to_remove.append(state_template_sync_id)

        return state_templates_proto


if __name__ == "__main__":
    import argparse
    import sys

    default_decoder_manifest_filename = "DecoderManifest.bin"
    default_collection_schemes_filename = "CollectionSchemeList.bin"
    parser = argparse.ArgumentParser(
        description="Generates decoder manifest and collection scheme Protobuf files"
    )
    parser.add_argument("--dbc-filename", metavar="FILE", help="CAN DBC filename")
    parser.add_argument(
        "--num-can-channels",
        metavar="NUM",
        default=1,
        help="Number of CAN channels, default: 1",
    )
    parser.add_argument("--obd-json-filename", metavar="FILE", help="OBD JSON filename")
    parser.add_argument(
        "--vision-system-data-json-filename",
        metavar="FILE",
        help="Vision System Data JSON filename",
    )
    parser.add_argument(
        "--campaign-json-filename",
        metavar="FILE",
        action="append",
        help="Campaign JSON filename",
    )
    parser.add_argument(
        "--decoder-manifest-filename",
        metavar="FILE",
        default=default_decoder_manifest_filename,
        help="Decoder manifest Protobuf output filename,"
        f" default: {default_decoder_manifest_filename}",
    )
    parser.add_argument(
        "--collection-schemes-filename",
        metavar="FILE",
        default=default_collection_schemes_filename,
        help="Collection schemes Protobuf output filename,"
        f" default: {default_collection_schemes_filename}",
    )
    parser.add_argument(
        "--guess-signal-types",
        action="store_true",
        help="Guess the signal data types",
    )
    parser.add_argument(
        "--custom-decoder-json-filename",
        metavar="FILE",
        action="append",
        help="Custom decoder JSON filename",
    )
    parser.add_argument(
        "--decoder-manifest-sync-id",
        metavar="ID",
        default="decoder_manifest_1",
        help="Custom decoder JSON filename",
    )
    parser.add_argument(
        "--node-json-filename",
        metavar="FILE",
        action="append",
        help="Node JSON filename for defining signal types",
    )
    parser.add_argument(
        "--fwe-config-json-filename",
        metavar="FILE",
        help=(
            "FWE config filename. When supplied the decoder manifest and collection schemes"
            " will be sent on MQTT to the configured topics"
        ),
    )
    parser.add_argument(
        "--iot-core-endpoint-url",
        metavar="URL",
        help="IoT Core endpoint URL. For use with --fwe-config-file",
        default=None,
    )
    parser.add_argument(
        "--run-time",
        metavar="SEC",
        help="Run time. For use with --fwe-config. Zero is infinite",
        default="0",
    )
    args = parser.parse_args()

    pf = ProtoFactory(
        can_dbc_file=args.dbc_filename,
        obd_json_file=args.obd_json_filename,
        guess_signal_types=args.guess_signal_types,
        number_of_can_channels=int(args.num_can_channels),
        vision_system_data_json_file=args.vision_system_data_json_filename,
        custom_decoder_json_files=args.custom_decoder_json_filename,
        decoder_manifest_sync_id=args.decoder_manifest_sync_id,
        node_json_files=args.node_json_filename,
    )

    with open(args.decoder_manifest_filename, "wb") as fp:
        fp.write(pf.decoder_manifest_proto.SerializeToString())
    with open(args.decoder_manifest_filename + ".json", "w") as fp:
        fp.write(MessageToJson(pf.decoder_manifest_proto))

    collection_schemes = []
    if args.campaign_json_filename:
        for filename in args.campaign_json_filename:
            with open(filename) as fp:
                collection_schemes += [json.load(fp)]
    pf.create_collection_schemes_proto(collection_schemes)
    with open(args.collection_schemes_filename, "wb") as fp:
        fp.write(pf.collection_schemes_proto.SerializeToString())
    with open(args.collection_schemes_filename + ".json", "w") as fp:
        fp.write(MessageToJson(pf.collection_schemes_proto))

    if args.fwe_config_json_filename:
        import binascii
        import os
        from concurrent.futures import Future

        import boto3
        from awscrt import auth, mqtt5
        from awsiot import mqtt5_client_builder
        from botocore.config import Config
        from snappy import snappy

        with open(args.fwe_config_json_filename) as fp:
            fwe_config = json.load(fp)

        mqtt_topic_prefix = fwe_config["staticConfig"]["mqttConnection"].get(
            "iotFleetWiseTopicPrefix"
        )
        if not mqtt_topic_prefix:
            print("Missing 'iotFleetWiseTopicPrefix' in config file. This is required to continue.")
            sys.exit(1)
        elif mqtt_topic_prefix.startswith("$"):
            print(
                "'iotFleetWiseTopicPrefix' config is a reserved topic."
                " This script only works with regular topics."
            )
            sys.exit(1)

        region = re.search(
            r"\.([^.]+)\.amazonaws.com", fwe_config["staticConfig"]["mqttConnection"]["endpointUrl"]
        ).group(1)
        session = boto3.Session()
        iot_client = session.client(
            "iot", endpoint_url=args.iot_core_endpoint_url, config=Config(region_name=region)
        )
        iot_endpoint = iot_client.describe_endpoint(endpointType="iot:Data-ATS")["endpointAddress"]
        credentials_provider = auth.AwsCredentialsProvider.new_default_chain()
        stop_future = Future()
        connection_success_future = Future()

        def on_lifecycle_stopped(data):
            stop_future.set_result(None)

        def on_lifecycle_connection_success(data):
            print("Connected to " + iot_endpoint)
            connection_success_future.set_result(None)

        def on_publish_received(message):
            payload = message.publish_packet.payload
            # Decompress if required:
            try:
                payload = snappy.decompress(payload)
            except Exception:
                pass
            try:
                msg = vehicle_data_pb2.VehicleData()
                msg.ParseFromString(payload)
                json_msg = MessageToJson(msg)
                msg = json.loads(json_msg)
                for signal in msg["capturedSignals"]:
                    signal["fullyQualifiedName"] = pf.get_fqn(signal["signalId"])
                    del signal["signalId"]
                print(json.dumps(msg, indent=2))
            except Exception as e:
                print(f"Error parsing message: {e}")

        mqtt_connection = mqtt5_client_builder.websockets_with_default_aws_signing(
            endpoint=iot_endpoint,
            region=region,
            on_publish_received=on_publish_received,
            on_lifecycle_connection_success=on_lifecycle_connection_success,
            on_lifecycle_stopped=on_lifecycle_stopped,
            credentials_provider=credentials_provider,
            client_id="protofactory-" + binascii.hexlify(os.urandom(5)).decode("utf-8"),
            clean_session=True,
            keep_alive_secs=30,
        )
        print("Connecting to " + iot_endpoint)
        mqtt_connection.start()
        connection_success_future.result(timeout=10)

        fwe_client_id = fwe_config["staticConfig"]["mqttConnection"]["clientId"]
        mqtt_topic = f"{mqtt_topic_prefix}vehicles/{fwe_client_id}/signals"
        print("Subscribing to " + mqtt_topic)
        subscribe_future = mqtt_connection.subscribe(
            subscribe_packet=mqtt5.SubscribePacket(
                subscriptions=[
                    mqtt5.Subscription(topic_filter=mqtt_topic, qos=mqtt5.QoS.AT_LEAST_ONCE)
                ]
            )
        )
        subscribe_res = subscribe_future.result(1000)
        print(f"Subscribed to {mqtt_topic} with {subscribe_res.reason_codes}")

        decoder_manifest_topic = f"{mqtt_topic_prefix}vehicles/{fwe_client_id}/decoder_manifests"
        print("Publishing to " + decoder_manifest_topic)
        publish_future = mqtt_connection.publish(
            mqtt5.PublishPacket(
                topic=decoder_manifest_topic,
                payload=pf.decoder_manifest_proto.SerializeToString(),
                qos=mqtt5.QoS.AT_LEAST_ONCE,
            )
        )
        publish_future.result(timeout=10)
        collection_schemes_topic = f"{mqtt_topic_prefix}vehicles/{fwe_client_id}/collection_schemes"
        print("Publishing to " + collection_schemes_topic)
        publish_future = mqtt_connection.publish(
            mqtt5.PublishPacket(
                topic=collection_schemes_topic,
                payload=pf.collection_schemes_proto.SerializeToString(),
                qos=mqtt5.QoS.AT_LEAST_ONCE,
            )
        )
        publish_future.result(timeout=10)

        run_time = 0
        try:
            while int(args.run_time) == 0 or run_time < int(args.run_time):
                time.sleep(1)
                run_time += 1
        except KeyboardInterrupt:
            pass

        mqtt_connection.stop()
        stop_future.result(timeout=10)

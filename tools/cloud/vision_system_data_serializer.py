# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import struct

# Map from vision-system-data to Python struct primitive types:
PRIMITIVE_TYPES = {
    "UINT8": "B",
    "BYTE": "B",
    "CHAR": "B",
    "BOOL": "B",
    "UINT16": "H",
    "UINT32": "I",
    "UINT64": "Q",
    "INT8": "b",
    "INT16": "h",
    "INT32": "i",
    "INT64": "q",
    "FLOAT32": "f",
    "FLOAT64": "d",
}

STRING_TYPES = {
    "STRING": "utf-8",
    "WSTRING": "utf-16",
}


class CdrDecoder:
    def __init__(self, buffer):
        self.buffer = buffer
        self.current_position = 0
        self.align_position = 0
        self.last_data_size = 0

    def reset_alignment(self):
        self.align_position = self.current_position

    def read_encapsulation(self):
        dummy = self.pop_primitive(PRIMITIVE_TYPES["UINT8"])
        if dummy != 0:
            raise Exception(f"unexpected non-zero initial byte: {dummy}")
        encapsulation_kind = self.pop_primitive(PRIMITIVE_TYPES["UINT8"])
        if encapsulation_kind != 1:
            raise Exception(f"unexpected encapsulation kind: {encapsulation_kind}")
        options = self.pop_primitive(PRIMITIVE_TYPES["UINT16"])
        if options != 0:
            raise Exception(f"unexpected options: {options}")
        self.reset_alignment()

    def get_alignment(self, data_size):
        return (
            0
            if (data_size <= self.last_data_size)
            else (
                (data_size - ((self.current_position - self.align_position) % data_size))
                & (data_size - 1)
            )
        )

    def realign(self, alignment):
        self.current_position += alignment
        self.buffer = self.buffer[alignment:]

    def pop_primitive(self, struct_type):
        dummy = struct.pack(struct_type, 0)
        alignment = self.get_alignment(len(dummy))
        aligned_size = len(dummy) + alignment
        if len(self.buffer) < aligned_size:
            raise Exception("out of data")
        self.last_data_size = len(dummy)
        self.realign(alignment)
        raw_bytes = self.buffer[0 : len(dummy)]
        self.buffer = self.buffer[len(dummy) :]
        self.current_position += len(dummy)
        return struct.unpack(struct_type, raw_bytes)[0]

    def pop_primitive_array(self, struct_type, length):
        dummy = struct.pack(struct_type, 0)
        alignment = self.get_alignment(len(dummy))
        aligned_size = len(dummy) * length + alignment
        if len(self.buffer) < aligned_size:
            raise Exception("out of data")
        self.last_data_size = len(dummy)
        self.realign(alignment)
        raw_bytes = self.buffer[0 : len(dummy) * length]
        self.buffer = self.buffer[len(dummy) * length :]
        self.current_position += len(dummy) * length
        return list(struct.unpack(str(length) + struct_type, raw_bytes))

    def pop_string(self, encoding):
        length = self.pop_primitive(PRIMITIVE_TYPES["UINT32"])
        code_unit_size = 1 if encoding == "utf-8" else 4
        if len(self.buffer) < length * code_unit_size:
            raise Exception("out of data")
        raw_bytes = self.buffer[0 : length * code_unit_size]
        self.buffer = self.buffer[length * code_unit_size :]
        self.last_data_size = code_unit_size
        self.current_position += len(raw_bytes)
        # utf-16 mode stores the 16-bit utf-16 'code units' in 32-bit words:
        if encoding == "utf-16":
            utf16_bytes = raw_bytes
            raw_bytes = bytearray()
            while len(utf16_bytes) > 0:
                raw_bytes += utf16_bytes[:2]
                utf16_bytes = utf16_bytes[4:]
        # utf-8 mode has a null-terminator:
        return raw_bytes.decode(encoding).rstrip("\0")


class CdrEncoder:
    def __init__(self):
        self.buffer = bytearray()
        self.current_position = 0
        self.align_position = 0
        self.last_data_size = 0

    def reset_alignment(self):
        self.align_position = self.current_position

    def write_encapsulation(self):
        self.push_primitive(PRIMITIVE_TYPES["UINT8"], 0)  # Dummy byte
        self.push_primitive(PRIMITIVE_TYPES["UINT8"], 1)  # Encapsulation kind
        self.push_primitive(PRIMITIVE_TYPES["UINT16"], 0)  # Options
        self.reset_alignment()

    def get_alignment(self, data_size):
        return (
            0
            if (data_size <= self.last_data_size)
            else (
                (data_size - ((self.current_position - self.align_position) % data_size))
                & (data_size - 1)
            )
        )

    def realign(self, alignment):
        self.current_position += alignment
        for _ in range(alignment):
            self.buffer.append(0)

    def push_primitive(self, struct_type, val):
        raw_bytes = struct.pack(struct_type, val)
        alignment = self.get_alignment(len(raw_bytes))
        self.last_data_size = len(raw_bytes)
        self.realign(alignment)
        self.buffer += raw_bytes
        self.current_position += len(raw_bytes)

    def push_primitive_array(self, struct_type, val):
        dummy = struct.pack(struct_type, 0)
        alignment = self.get_alignment(len(dummy))
        self.last_data_size = len(dummy)
        self.realign(alignment)
        raw_bytes = struct.pack(str(len(val)) + struct_type, *val)
        self.buffer += raw_bytes
        self.current_position += len(raw_bytes)

    def push_string(self, encoding, val, upper_bound):
        if upper_bound > 0 and len(val) > upper_bound:
            raise Exception(f'length of string "{val}" exceeds upper bound {upper_bound}')
        if encoding == "utf-8":
            code_unit_size = 1
            # utf-8 mode has a null-terminator:
            val += "\0"
        else:
            code_unit_size = 2
        raw_bytes = val.encode(encoding)
        self.push_primitive(PRIMITIVE_TYPES["UINT32"], int(len(raw_bytes) / code_unit_size))
        # utf-16 mode stores the 16-bit utf-16 'code units' in 32-bit words:
        if encoding == "utf-16":
            code_unit_size = 4
            utf16_bytes = raw_bytes
            raw_bytes = bytearray()
            while len(utf16_bytes) > 0:
                raw_bytes += utf16_bytes[:2]
                raw_bytes += bytearray([0, 0])
                utf16_bytes = utf16_bytes[2:]
        self.buffer += raw_bytes
        self.last_data_size = code_unit_size
        self.current_position += len(raw_bytes)


class VisionSystemDataSerializer:
    def __init__(self, decoder_manifest):
        self.decoder_manifest = decoder_manifest

    def get_signal_definition(self, fully_qualified_name):
        for signal_definition in self.decoder_manifest:
            if signal_definition["fullyQualifiedName"] == fully_qualified_name:
                return signal_definition
        raise Exception(f"unknown message: {fully_qualified_name}")

    def serialize(self, fully_qualified_name, msg):
        signal_definition = self.get_signal_definition(fully_qualified_name)
        cdr_encoder = CdrEncoder()
        cdr_encoder.write_encapsulation()
        self.encode_message(
            cdr_encoder, signal_definition["messageSignal"]["structuredMessage"], msg
        )
        return bytes(cdr_encoder.buffer)

    def get_member_type(self, message_definition):
        member_type = message_definition["structuredMessageListDefinition"]["memberType"]
        if "primitiveMessageDefinition" not in member_type:
            return member_type
        primitive_type = member_type["primitiveMessageDefinition"][
            "ros2PrimitiveMessageDefinition"
        ]["primitiveType"]
        if primitive_type not in PRIMITIVE_TYPES:
            return member_type
        return primitive_type

    def encode_message(self, cdr_encoder, message_definition, msg):
        if "primitiveMessageDefinition" in message_definition:
            primitive_type = message_definition["primitiveMessageDefinition"][
                "ros2PrimitiveMessageDefinition"
            ]["primitiveType"]
            if primitive_type in PRIMITIVE_TYPES:
                cdr_encoder.push_primitive(PRIMITIVE_TYPES[primitive_type], msg)
            elif primitive_type in STRING_TYPES:
                upper_bound = message_definition["primitiveMessageDefinition"][
                    "ros2PrimitiveMessageDefinition"
                ].get("upperBound", 0)
                cdr_encoder.push_string(STRING_TYPES[primitive_type], msg, upper_bound)
            else:
                raise Exception(f"unknown primitive type: {primitive_type}")
        elif "structuredMessageListDefinition" in message_definition:
            list_type = message_definition["structuredMessageListDefinition"]["listType"]
            if list_type in ["DYNAMIC_BOUNDED_CAPACITY", "FIXED_CAPACITY"]:
                capacity = message_definition["structuredMessageListDefinition"]["capacity"]
                if list_type == "DYNAMIC_BOUNDED_CAPACITY" and len(msg) > capacity:
                    raise Exception(f"length of list exceeds capacity {capacity}")
                if list_type == "FIXED_CAPACITY" and len(msg) != capacity:
                    raise Exception(f"length of list does not match capacity {capacity}")
            if list_type in ["DYNAMIC_UNBOUNDED_CAPACITY", "DYNAMIC_BOUNDED_CAPACITY"]:
                cdr_encoder.push_primitive(PRIMITIVE_TYPES["UINT32"], len(msg))
            member_type = self.get_member_type(message_definition)
            # Optimize primitive arrays:
            if not isinstance(member_type, dict) and member_type in PRIMITIVE_TYPES:
                cdr_encoder.push_primitive_array(PRIMITIVE_TYPES[member_type], msg)
            else:
                for element in msg:
                    self.encode_message(cdr_encoder, member_type, element)
        elif "structuredMessageDefinition" in message_definition:
            for field_definition in message_definition["structuredMessageDefinition"]:
                self.encode_message(
                    cdr_encoder, field_definition["dataType"], msg[field_definition["fieldName"]]
                )
        else:
            raise Exception(f"unknown message type: {message_definition}")

    def deserialize(self, fully_qualified_name, cdr_data):
        signal_definition = self.get_signal_definition(fully_qualified_name)
        cdr_decoder = CdrDecoder(cdr_data)
        cdr_decoder.read_encapsulation()
        return self.decode_message(
            cdr_decoder, signal_definition["messageSignal"]["structuredMessage"]
        )

    def decode_message(self, cdr_decoder, message_definition):
        if "primitiveMessageDefinition" in message_definition:
            primitive_type = message_definition["primitiveMessageDefinition"][
                "ros2PrimitiveMessageDefinition"
            ]["primitiveType"]
            if primitive_type in PRIMITIVE_TYPES:
                return cdr_decoder.pop_primitive(PRIMITIVE_TYPES[primitive_type])
            elif primitive_type in STRING_TYPES:
                return cdr_decoder.pop_string(STRING_TYPES[primitive_type])
            else:
                raise Exception(f"unknown primitive type: {primitive_type}")
        elif "structuredMessageListDefinition" in message_definition:
            list_type = message_definition["structuredMessageListDefinition"]["listType"]
            if list_type in ["DYNAMIC_UNBOUNDED_CAPACITY", "DYNAMIC_BOUNDED_CAPACITY"]:
                list_length = cdr_decoder.pop_primitive(PRIMITIVE_TYPES["UINT32"])
            else:
                list_length = message_definition["structuredMessageListDefinition"]["capacity"]
            member_type = self.get_member_type(message_definition)
            # Optimize primitive arrays:
            if not isinstance(member_type, dict) and member_type in PRIMITIVE_TYPES:
                return cdr_decoder.pop_primitive_array(PRIMITIVE_TYPES[member_type], list_length)
            else:
                result = []
                for _ in range(list_length):
                    result.append(self.decode_message(cdr_decoder, member_type))
                return result
        elif "structuredMessageDefinition" in message_definition:
            result = {}
            for field_definition in message_definition["structuredMessageDefinition"]:
                result[field_definition["fieldName"]] = self.decode_message(
                    cdr_decoder, field_definition["dataType"]
                )
            return result
        else:
            raise Exception(f"unknown message type: {message_definition}")

#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import asyncio
import struct
import time
from threading import Thread

import can


class CanCommandServer:
    command_configs = [
        {
            "canRequestId": 0x00000123,  # standard 11-bit CAN ID
            "canResponseId": 0x00000456,  # standard 11-bit CAN ID
            "actuatorName": "Vehicle.actuator6",
            "argumentType": ">i",  # big-endian int32
            "inProgressCount": 0,
            "status": 1,  # SUCCEEDED
            "reasonCode": 0x1234,
            "reasonDescription": "hello",
            "responseDelay": 1,
        },
        {
            "canRequestId": 0x80000789,  # extended 29-bit CAN ID (MSB sets extended)
            "canResponseId": 0x80000ABC,  # extended 29-bit CAN ID (MSB sets extended)
            "actuatorName": "Vehicle.actuator7",
            "argumentType": ">d",  # big-endian double
            "inProgressCount": 10,
            "status": 1,  # SUCCEEDED
            "reasonCode": 0x5678,
            "reasonDescription": "goodbye",
            "responseDelay": 1,
        },
    ]
    args = {}  # used to record the passed arguments for each execution

    def is_extended_id(self, can_id):
        return (can_id & 0x80000000) != 0

    def mask_id(self, can_id):
        return (0x1FFFFFFF if self.is_extended_id(can_id) else 0x7FF) & can_id

    def pop_string(self):
        for i in range(len(self.data)):
            if self.data[i] == 0x00:
                break
        else:
            raise RuntimeError("Could not find null terminator")
        string_data = self.data[0:i]
        self.data = self.data[i + 1 :]
        return string_data.decode("utf-8")

    def pop_arg(self, struct_type):
        dummy = struct.pack(struct_type, 0)
        arg_data = self.data[0 : len(dummy)]
        self.data = self.data[len(dummy) :]
        return struct.unpack(struct_type, arg_data)[0]

    def push_string(self, val):
        val += "\0"
        self.data += val.encode("utf-8")

    def push_arg(self, struct_type, val):
        self.data += struct.pack(struct_type, val)

    def handle_message(self, receive_msg):
        for config in self.command_configs:
            if receive_msg.arbitration_id == self.mask_id(
                config["canRequestId"]
            ) and receive_msg.is_extended_id == self.is_extended_id(config["canRequestId"]):
                break
        else:
            return

        try:
            self.data = bytearray(receive_msg.data)
            command_id = self.pop_string()
            issued_timestamp_ms = self.pop_arg(">Q")
            execution_timeout_ms = self.pop_arg(">Q")
            arg_value = self.pop_arg(config["argumentType"])
            print(
                f"Received request for {config['actuatorName']}"
                f" with command id {command_id}, value {arg_value},"
                f" issued timestamp {issued_timestamp_ms}, execution timeout {execution_timeout_ms}"
            )
            self.args[command_id] = arg_value
            execution_state = {"in_progress_counter": config["inProgressCount"]}

            def send_response():
                status = 10 if execution_state["in_progress_counter"] > 0 else config["status"]
                print(
                    f"Sending response to request for {config['actuatorName']}"
                    f" with command id {command_id}, status {status},"
                    f" reason code {config['reasonCode']},"
                    f" reason description {config['reasonDescription']}"
                )
                self.data = bytearray()
                send_msg = can.Message()
                send_msg.arbitration_id = self.mask_id(config["canResponseId"])
                send_msg.is_extended_id = self.is_extended_id(config["canResponseId"])
                send_msg.is_fd = True
                self.push_string(command_id)
                self.push_arg("B", status)
                self.push_arg(">I", config["reasonCode"])
                self.push_string(config["reasonDescription"])
                send_msg.data = self.data
                send_msg.dlc = len(self.data)
                self.can_bus.send(send_msg)
                if execution_state["in_progress_counter"] > 0:
                    execution_state["in_progress_counter"] -= 1
                    self.loop.call_later(config["responseDelay"], send_response)

            self.loop.call_later(config["responseDelay"], send_response)
        except Exception as e:
            print(e)

    def __init__(self, interface):
        self.can_bus = can.Bus(interface, interface="socketcan", fd=True)
        self.loop = asyncio.new_event_loop()
        can.Notifier(bus=self.can_bus, listeners=[self.handle_message], loop=self.loop)
        self.thread = Thread(name="CanCommandServer", target=self.loop.run_forever)
        self.thread.start()

    def stop(self):
        async def stop_loop():
            self.loop.stop()

        asyncio.run_coroutine_threadsafe(stop_loop(), self.loop)
        self.thread.join()
        self.can_bus.shutdown()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Runs a server to respond to CAN command messages")
    parser.add_argument("-i", "--interface", required=True, help="CAN interface, e.g. vcan0")
    args = parser.parse_args()
    command_server = CanCommandServer(args.interface)
    try:
        while True:
            time.sleep(60)
    except KeyboardInterrupt:
        pass
    command_server.stop()

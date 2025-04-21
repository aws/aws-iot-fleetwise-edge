#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import array
import importlib
import json
import re
import traceback
from threading import Thread

import numpy
import rclpy
from rclpy.node import Node


class Rosigen:
    PRIMITIVE_TYPES = (int, str, bool, float, bytes)
    PUBLISH_HISTORY_DEPTH = 10  # Message queue size with 'keep last' QoS mode

    def __init__(self, config_filename, values_filename=None):
        rclpy.init()
        self._node = Node("Rosigen")
        self._publishers = {}  # Dict of publishers, indexed by ROS2 topic name
        self._timers = {}  # Dict of timers used to trigger publishing, index by ROS2 topic name
        # Dict of ROS2 message values, indexed by ROS2 topic name. Each message is of a ROS2 message
        # type, structured according to the corresponding interface definition.
        self._vals = {}
        config = self._load_json(config_filename)
        for topic in config["topics"]:
            module = importlib.import_module(topic["module"])
            topic_type = getattr(module, topic["type"])
            self._publishers[topic["name"]] = self._node.create_publisher(
                topic_type, topic["name"], self.PUBLISH_HISTORY_DEPTH
            )
            self._vals[topic["name"]] = topic_type()
            self._timers[topic["name"]] = self._node.create_timer(
                topic["period_sec"],
                lambda name=topic["name"]: self.publish_single_message(name),
            )
        if values_filename is not None:
            self.load_values(values_filename)
        self._thread = Thread(target=self._publish_thread)
        self._thread.start()

    def publish_single_message(self, name):
        # print("Publish single message of size: "+str(len(str(self._vals[name]))))
        self._publishers[name].publish(self._vals[name])

    def _publish_thread(self):
        try:
            rclpy.spin(self._node)
        except rclpy.executors.ExternalShutdownException:
            pass

    def _is_list(self, item):
        return type(item) in [numpy.ndarray, list, array.array]

    def _is_primitive_list(self, member):
        return (
            type(member) in [numpy.ndarray, array.array]
            or type(member) is list
            and len(member) > 0
            and type(member[0]) in self.PRIMITIVE_TYPES
        )

    def _save_values(self, msg):
        obj = {}
        for field in msg.get_fields_and_field_types():
            field_val = getattr(msg, field)
            if type(field_val) in self.PRIMITIVE_TYPES:
                obj[field] = field_val
            elif self._is_primitive_list(field_val):
                obj[field] = []
                for member in field_val:
                    if type(member).__module__ == numpy.__name__:
                        member = member.item()
                    obj[field].append(member)
            elif self._is_list(field_val):
                obj[field] = []
                for member in field_val:
                    obj[field].append(self._save_values(member))
            else:
                obj[field] = self._save_values(field_val)
        return obj

    def save_values(self, filename):
        vals = {}
        for topic in self._vals:
            vals[topic] = self._save_values(self._vals[topic])
        self._save_json(filename, vals)

    def _check_primitive_type(self, field_val, val):
        if type(field_val).__module__ == numpy.__name__:
            field_val = field_val.item()
        if type(field_val) not in self.PRIMITIVE_TYPES:
            raise Exception("error: invalid path")
        # If field is Boolean, check whether the value is false ('0', 'False' or 'false'), then set
        # the value to '' (blank string), so that type(field_type)(val) will return False:
        if type(field_val) == bool and val in ["0", "False", "false"]:
            val = ""
        if type(field_val) == bytes:
            val = [int(val)]
        return val

    def _set_value(self, msg, field, val):
        if self._is_list(msg):
            match = re.match(r"^\[(\d+)\]$", field)
            if not match:
                raise Exception("error: invalid index")
            index = int(match.groups(1)[0])
            if len(msg) <= index:
                # for dynamic sized arrays first grow vals
                for _i in range(0, index - len(msg) + 1):
                    msg.append(val)
            val = self._check_primitive_type(msg[index], val)
            msg[index] = type(msg[index])(val)
        else:
            field_val = getattr(msg, field)
            val = self._check_primitive_type(field_val, val)
            setattr(msg, field, type(field_val)(val))

    def _load_values(self, msg, val):
        for field in msg.get_fields_and_field_types():
            field_val = getattr(msg, field)
            if type(field_val) in self.PRIMITIVE_TYPES:
                self._set_value(msg, field, val[field])
            elif self._is_primitive_list(field_val):
                length = len(field_val)
                if length == 0:
                    # dynamic size take length of input
                    length = len(val[field])
                for i in range(length):
                    self._set_value(field_val, f"[{i}]", val[field][i])
            elif self._is_list(field_val):
                for i in range(len(field_val)):
                    self._load_values(field_val[i], val[field][i])
            else:
                self._load_values(field_val, val[field])

    def load_values(self, filename):
        vals = self._load_json(filename)
        for topic in self._vals:
            self._load_values(self._vals[topic], vals[topic])

    def _get_fields(self, msg):
        obj = {}
        for field in msg.get_fields_and_field_types():
            field_val = getattr(msg, field)
            if type(field_val) in self.PRIMITIVE_TYPES:
                obj[field] = None  # No further auto-completion
            elif self._is_primitive_list(field_val):
                obj[field] = {}
                for i in range(len(field_val)):
                    obj[field][f"[{i}]"] = None
            elif self._is_list(field_val):
                obj[field] = {}
                for i in range(len(field_val)):
                    obj[field][f"[{i}]"] = self._get_fields(field_val[i])
            else:
                obj[field] = self._get_fields(field_val)
        return obj

    def get_fields(self):
        return {topic: self._get_fields(self._vals[topic]) for topic in self._vals}

    def get_value(self, path):
        try:
            msg = self._vals[path[0]]
            for i in range(1, len(path)):
                if self._is_list(msg):
                    match = re.match(r"^\[(\d+)\]$", path[i])
                    if not match:
                        raise Exception("error: invalid index")
                    msg = msg[int(match.groups(1)[0])]
                else:
                    msg = getattr(msg, path[i])
            return msg
        except Exception:
            print("error: invalid path")
            raise

    def set_value(self, path, value):
        msg = self.get_value(path[:-1])
        self._set_value(msg, path[-1], value)

    def _load_json(self, filename):
        try:
            with open(filename) as fp:
                return json.load(fp)
        except Exception:
            print("error: failed to load " + filename)
            raise

    def _save_json(self, filename, data):
        try:
            with open(filename, "w") as fp:
                return json.dump(data, fp, sort_keys=True, indent=4)
        except Exception:
            print("error: failed to save " + filename)

    def stop(self):
        # Destroying the node is not really necessary since it will be destroyed when garbage
        # collected. But explicitly destroying makes it more predictable and also allows us to
        # reinitialize ROS2 with a different config.
        self._node.destroy_node()
        # After ROS2 Humble, rclpy.shutdown() could fail on SIGTERM because ROS2 signal handler
        # already called it. That is why we use try_shutdown() instead, which checks whether the
        # context is already shutdown.
        rclpy.utilities.try_shutdown()
        self._thread.join()

    def topic(self, topic):
        return self._vals[topic]


if __name__ == "__main__":
    import argparse

    from prompt_toolkit import PromptSession
    from prompt_toolkit.completion import NestedCompleter, PathCompleter

    parser = argparse.ArgumentParser(description="Generates ROS2 messages interactively")
    parser.add_argument("-c", "--config", help="Config JSON file", required=True)
    parser.add_argument(
        "-v", "--values", help="Values JSON file. Generate one using the 'save' command."
    )
    args = parser.parse_args()

    r = Rosigen(args.config, args.values)

    path_completer = PathCompleter()
    cmd_completion_dict = {
        "set": r.get_fields(),
        "get": r.get_fields(),
        "save": path_completer,
        "load": path_completer,
        "exit": None,
    }
    cmd_completer = NestedCompleter.from_nested_dict(cmd_completion_dict)

    def print_help():
        print("Usage:")
        print("    set <TOPIC> <MSG_MEMBERS...> <VALUE>")
        print("    get <TOPIC> <MSG_MEMBERS...>")
        print("    save <VALUE_JSON_FILE>")
        print("    load <VALUE_JSON_FILE>")
        print("    exit")

    session = PromptSession()
    try:
        while True:
            cmd = session.prompt("rosigen$ ", completer=cmd_completer).split()
            try:
                if len(cmd) == 0:
                    pass
                elif cmd[0] == "exit" or cmd[0] == "quit":
                    break
                elif cmd[0] == "set":
                    r.set_value(cmd[1:-1], cmd[-1])
                elif cmd[0] == "get":
                    print(r.get_value(cmd[1:]))
                elif cmd[0] == "save":
                    r.save_values(cmd[1])
                elif cmd[0] == "load":
                    r.load_values(cmd[1])
                elif cmd[0] == "help":
                    print_help()
                else:
                    print("error: invalid command: " + cmd[0])
                    print_help()
            except Exception as e:
                print("TRACEBACK: " + traceback.format_exc())
                print(e)
    except KeyboardInterrupt:
        pass
    r.stop()

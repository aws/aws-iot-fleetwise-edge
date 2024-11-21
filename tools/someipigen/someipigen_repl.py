#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import someipigen
from prompt_toolkit import PromptSession
from prompt_toolkit.completion import WordCompleter


class SomeipigenRepl:
    def __init__(self, domain, instance, connection, values_filename=None):
        self.signal_holder = someipigen.SignalManager()
        if values_filename:
            self.load_values(values_filename)
        self.signal_holder.start(domain, instance, connection)
        self.running = True

    def set_value(self, signal, value):
        existing_value = self.signal_holder.get_value(signal)
        if existing_value is None:
            raise Exception(f"No existing value for {signal}")
        elif isinstance(existing_value, int):
            value = int(value)
        elif isinstance(existing_value, float):
            value = float(value)
        elif isinstance(existing_value, str):
            value = str(value)
        elif isinstance(existing_value, bool):
            value = bool(value)
        else:
            raise Exception(f"Unsupported type for {signal}: {type(existing_value)}")
        self.signal_holder.set_value(signal, value)

    def get_value(self, signal):
        return self.signal_holder.get_value(signal)

    def save_values(self, filename):
        self.signal_holder.save_values(filename)

    def load_values(self, filename):
        self.signal_holder.load_values(filename)

    def stop(self):
        self.running = False
        self.signal_holder.stop()

    def _get_signals(self):
        return self.signal_holder.get_signals()


if __name__ == "__main__":
    session = PromptSession()

    someipigen_repl = SomeipigenRepl(
        domain="local", instance="commonapi.ExampleSomeipInterface", connection="someipigen"
    )
    signals = someipigen_repl._get_signals()
    commands = ["set", "get", "save", "load", "help", "exit", "quit", "list"]
    command_completer = WordCompleter(commands + signals)

    while someipigen_repl.running:
        try:
            text = session.prompt("someipigen$ ", completer=command_completer).strip()
            if not text:
                continue

            cmd, *parts = text.split(maxsplit=1)

            if cmd == "set":
                if len(parts) == 1 and " " in parts[0]:
                    signal, value = parts[0].split(maxsplit=1)
                    someipigen_repl.set_value(signal, value)
                else:
                    print("Usage: set [signal] [value]")
            elif cmd == "get" and len(parts) == 1:
                print(someipigen_repl.get_value(parts[0]))
            elif cmd == "save" and len(parts) == 1:
                someipigen_repl.save_values(parts[0])
            elif cmd == "load" and len(parts) == 1:
                someipigen_repl.load_values(parts[0])
            elif cmd == "list":
                signals = someipigen_repl._get_signals()
                for signal in signals:
                    print(signal)
            elif cmd in ["help", "exit", "quit"]:
                if cmd == "help":
                    print("Available commands: set, get, save, load, help, exit, quit, list")
                else:
                    someipigen_repl.stop()
            else:
                print("Unknown command. Type 'help' for a list of commands.")
        except (EOFError, KeyboardInterrupt):
            someipigen_repl.stop()
        except Exception as e:
            print(f"Error: {e}")

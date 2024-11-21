#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import someip_device_shadow_editor
from prompt_toolkit import PromptSession
from prompt_toolkit.completion import WordCompleter


class SomeipDeviceShadowEditorRepl:
    def __init__(self, domain, instance, connection):
        self.editor = someip_device_shadow_editor.DeviceShadowOverSomeipExampleApplication()
        self.editor.init(domain, instance, connection)
        self.running = True

    def stop(self):
        self.running = False
        self.editor.deinit()


if __name__ == "__main__":
    session = PromptSession()

    someip_device_shadow_editor_repl = SomeipDeviceShadowEditorRepl(
        domain="local",
        instance="commonapi.DeviceShadowOverSomeipInterface",
        connection="someip_device_shadow_editor",
    )

    commands = ["get", "update", "delete", "help", "exit"]
    command_completer = WordCompleter(commands)

    while someip_device_shadow_editor_repl.running:
        try:
            text = session.prompt("someip_device_shadow_editor$ ", completer=command_completer)

            # Split on every space, with only up to 3 arguments:
            pos = 0
            cmd = []
            while pos <= len(text) and len(cmd) < 3:
                next_pos = text.find(" ", pos)
                if next_pos < 0:
                    next_pos = len(text)
                cmd.append(text[pos:next_pos])
                pos = next_pos + 1

            if len(cmd) == 0:
                pass
            elif cmd[0] == "get" and len(cmd) == 2:
                print("result: " + someip_device_shadow_editor_repl.editor.get_shadow(cmd[1]))
            elif cmd[0] == "update" and len(cmd) >= 3:
                print(
                    "result: "
                    + someip_device_shadow_editor_repl.editor.update_shadow(cmd[1], cmd[2])
                )
            elif cmd[0] == "delete" and len(cmd) == 2:
                someip_device_shadow_editor_repl.editor.delete_shadow(cmd[1])
            elif cmd[0] == "help":
                print("Available commands:")
                print("  get <SHADOW_NAME>")
                print("  update <SHADOW_NAME> <UPDATE_DOCUMENT>")
                print("  delete <SHADOW_NAME>")
            elif cmd[0] in ["exit", "quit"]:
                break
            else:
                print("Error: invalid command")
        except (EOFError, KeyboardInterrupt):
            break
        except Exception as e:
            print(f"Error: {e}")
    someip_device_shadow_editor_repl.stop()

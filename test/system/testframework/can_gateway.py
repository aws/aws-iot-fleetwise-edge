# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
from typing import List

from testframework.process_utils import SubprocessHelper

log = logging.getLogger(__name__)


class CanGateway:
    def __init__(self):
        self._subprocess_helper = SubprocessHelper(as_root_user=True)

    def add_rule(self, source_channel: str, destination_channel: str):
        gateway_command = [
            "cangw",
            "-A",
            "-s",
            source_channel,
            "-d",
            destination_channel,
            "-e",
        ]
        self._subprocess_helper.target_check_call(gateway_command, timeout=30)

    def delete_rule(self, source_channel: str, destination_channel: str):
        current_rules = self.list_rules()
        log.info(f"Current gateway rules: {current_rules}")

        for rule in current_rules:
            if source_channel in rule or destination_channel in rule:
                self._subprocess_helper.target_check_call(["cangw", "-D", *rule])

    def list_rules(self) -> List[List[str]]:
        # For some reason cangw -L returns 36 even on success. So make it sure the script returns 0
        # in such case.
        gateway_command = "cangw -L || retcode=$?; if [ $retcode != 36 ]; then exit $retcode; fi"

        output = (
            self._subprocess_helper.target_check_output(list(gateway_command.split()), shell=True)
            .strip()
            .splitlines()
        )
        rules = []
        for line in output:
            if "cangw -A" in line:
                line = line.replace("cangw -A", "").strip()
                line = line[: line.find("#")]
                rules.append(line.split())

        return rules

    def reset(self):
        self._subprocess_helper.target_check_call(["cangw", "-F"])

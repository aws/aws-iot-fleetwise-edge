# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from typing import Optional

from testframework.can_gateway import CanGateway
from testframework.common import is_hil
from testframework.process_utils import SubprocessHelper

log = logging.getLogger(__name__)


class NetworkNamespace:
    def __init__(self, worker_number: int):
        self._subprocess_helper = SubprocessHelper(as_root_user=True)
        self._can_gateway = CanGateway()
        self.name = f"ns{worker_number}"
        self.exec = ["ip", "netns", "exec", self.name]
        self.vpeer = f"vpeer{worker_number}"
        self.vxcanpeer = f"vxcanpeer{worker_number}"
        self.veth_addr = f"10.200.{worker_number+1}.1"
        self.source_channel = "can0" if is_hil() else f"vcan{worker_number}"
        self.destination_channel = f"vxcan{worker_number}"

        self.link_up()
        self.reset_throttling()
        self._check_internet_working()

        self._can_gateway.delete_rule(self.source_channel, self.destination_channel)
        self._can_gateway.add_rule(self.source_channel, self.destination_channel)

    def get_can_dev(self):
        return self.vxcanpeer

    def link_down(self):
        self._subprocess_helper.target_check_call(
            self.exec + ["ip", "link", "set", "dev", self.vpeer, "down"]
        )

    def link_up(self):
        self._subprocess_helper.target_check_call(
            self.exec + ["ip", "link", "set", "dev", self.vpeer, "up"]
        )
        self._subprocess_helper.target_check_call(
            self.exec + ["ip", "route", "replace", "default", "via", self.veth_addr]
        )

    def configure_throttling(
        self,
        *,
        loss_percentage: Optional[int] = None,
        delay_ms: Optional[int] = None,
        rate_kbit: Optional[int] = None,
    ):
        # First we need to delete the previous qdisc, otherwise previous throttling settings
        # will persist even if we use `replace dev` (e.g. if we previously called this method
        # with rate_kbit=200, then later we call it with delay=0, the resulting qdisc
        # will still throttle by rate).
        self.reset_throttling()

        if loss_percentage is None and delay_ms is None and rate_kbit is None:
            raise ValueError("No valid argument passed to configure throttling")
        loss = []
        delay = []
        rate = []
        if loss_percentage is not None:
            loss = ["loss", f"{loss_percentage}%"]
        if delay_ms is not None:
            delay = ["delay", f"{delay_ms}ms"]
        if rate_kbit is not None:
            rate = ["rate", f"{rate_kbit}kbit"]
        self._subprocess_helper.target_check_call(
            self.exec
            + ["tc", "qdisc", "replace", "dev", self.vpeer, "root", "netem"]
            + loss
            + delay
            + rate
        )

    def reset_throttling(self):
        current_qdisc = self._subprocess_helper.target_check_output(
            self.exec + ["tc", "qdisc", "show", "dev", self.vpeer]
        )
        if "netem" in current_qdisc:
            self._subprocess_helper.target_check_call(
                self.exec + ["tc", "qdisc", "del", "dev", self.vpeer, "root", "netem"]
            )

    def get_exec_list(self):
        return self.exec.copy()

    def _check_internet_working(self):
        log.info("Checking internet connection...")
        for i in range(10):
            try:
                self._subprocess_helper.target_check_call(
                    self.exec + ["ping", "-c", "1", "amazon.com"], timeout=10
                )
                break
            except Exception:
                if i >= 9:
                    raise
                log.info("No internet. Waiting 10s then trying again...")
                time.sleep(10)

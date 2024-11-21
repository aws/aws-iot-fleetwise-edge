#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import asyncio
import logging
import socket
import textwrap

import can
from someip.sd import ServiceDiscoveryProtocol
from someip.service import SimpleEventgroup, SimpleService

log = logging.getLogger(__name__)


class CanEventGroup(SimpleEventgroup):
    def __init__(self, service: SimpleService, event_group_id: int, event_id: int):
        super().__init__(service, id=event_group_id, interval=None)
        self._event_id = event_id
        self._queue = asyncio.Queue()

    async def add_message(self, data: bytes):
        # We can't just send the data here because we need to set the data in self.values and
        # then call self.notify_once. But self.notify_once happens asynchronously and it doesn't
        # copy the data. So by the time it sends the data it might be already overwritten.
        # We need to queue it and have them sent one by one by a single coroutine.
        await self._queue.put(data)

    async def run_send_messages(self):
        try:
            while True:
                data = await self._queue.get()
                self.values[self._event_id] = data
                # Using the non-public self._notify_all method as self.notify_once doesn't return
                # the coroutine so we can't wait for it to finish.
                await self._notify_all(events=[self._event_id], label="event")
        except asyncio.CancelledError:
            pass


class CanListener(can.Listener):
    def __init__(self, event_loop: asyncio.AbstractEventLoop, can_service) -> None:
        super().__init__()
        self._event_loop = event_loop
        self._can_service = can_service

    def on_message_received(self, message: can.Message):
        timestamp_us = int(message.timestamp * 1e6)
        payload = b"".join(
            [
                message.arbitration_id.to_bytes(4, "big"),
                timestamp_us.to_bytes(8, "big"),
                bytes(message.data),
            ]
        )
        asyncio.run_coroutine_threadsafe(
            self._can_service.event_group.add_message(payload), self._event_loop
        )


async def create_service(
    local_addr: str,
    multicast_addr: str,
    port: int,
    service_id: int,
    instance_id: int,
    event_id: int,
    event_group_id: int,
):
    log.info(
        "Creating SOME/IP service"
        f" service_id=0x{service_id:02X}"
        f" instance_id=0x{instance_id:02X}"
        f" event_id=0x{event_id:02X}"
        f" event_group_id=0x{event_group_id:02X}"
    )
    sd_trsp_u, sd_trsp_m, sd_prot = await ServiceDiscoveryProtocol.create_endpoints(
        family=socket.AF_INET, local_addr=local_addr, multicast_addr=multicast_addr
    )
    sd_prot.timings.CYCLIC_OFFER_DELAY = 2

    class_service_id = service_id

    class CanService(SimpleService):
        service_id = class_service_id  # This can only be set as class variable
        version_major = 0  # interface_version
        version_minor = 0

        def __init__(self, instance_id: int, event_group_id: int, event_id: int):
            super().__init__(instance_id)
            self.event_group = CanEventGroup(self, event_group_id=event_group_id, event_id=event_id)
            self.register_eventgroup(self.event_group)

    can_service: CanService
    _, can_service = await CanService.create_unicast_endpoint(
        instance_id=instance_id,
        local_addr=(local_addr, port),
        event_group_id=event_group_id,
        event_id=event_id,
    )
    can_service.start_announce(sd_prot.announcer)

    return sd_trsp_u, sd_trsp_m, sd_prot, can_service


async def run(sd_trsp_u, sd_trsp_m, sd_prot, can_service):
    sd_prot.start()

    asyncio.get_event_loop().create_task(can_service.event_group.run_send_messages())

    try:
        while True:
            await asyncio.sleep(1)
    except asyncio.CancelledError:
        pass
    finally:
        sd_prot.stop()
        sd_trsp_u.close()
        sd_trsp_m.close()
        can_service.stop()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)

    class CustomArgumentParser(argparse.ArgumentParser):
        def format_help(self):
            return super().format_help() + textwrap.dedent(
                """
                The CAN data and metadata are sent as SOME/IP payload in the following format:
                 _________________________________________________________
                |   CAN ID  |  Timestamp (in us)  |       CAN data        |
                |___________|_____________________|_______________________|
                   4 bytes         8 bytes             variable length

                CAN ID and Timestamp are unsigned integers encoded in network byte order (big endian).

                CAN ID is in the SocketCAN format: https://github.com/linux-can/can-utils/blob/88f0c753343bd863dd3110812d6b4698c4700b26/include/linux/can.h#L66-L78
                """  # noqa: E501
            )

    parser = CustomArgumentParser(
        prog="can-to-someip",
        description="Listen to CAN messages and offer them as a SOME/IP service",
    )
    parser.add_argument(
        "-i",
        "--can-interface",
        required=True,
        help="The CAN interface to listen to, e.g. vcan0",
    )
    parser.add_argument(
        "--local-addr",
        required=True,
        help="The unicast IP address of this SOME/IP service. It should match the address of an"
        " existing network interface.",
    )
    parser.add_argument(
        "--local-port",
        type=int,
        default=0,
        help="The port this SOME/IP service will listen to",
    )
    parser.add_argument(
        "--multicast-addr",
        required=True,
        help="The multicast address that will be used for service discovery, e.g. 224.224.224.245",
    )
    parser.add_argument(
        "--service-id",
        type=int,
        default=0x7777,
        help="The service id that will be announced to other SOME/IP applications",
    )
    parser.add_argument(
        "--instance-id",
        type=int,
        default=0x5678,
        help="The instance id of this service that will be announced. Only a single instance will"
        " be created.",
    )
    parser.add_argument(
        "--event-id",
        type=int,
        default=0x8778,
        metavar="[0x8000-0xFFFE]",
        help="ID of SOME/IP event that will be offered."
        " All CAN data is sent with the same event ID.",
    )
    parser.add_argument(
        "--event-group-id",
        type=int,
        default=0x5555,
        help="ID of SOME/IP event group that will be offered."
        " Other applications will be able to subscribe to this event group.",
    )
    args = parser.parse_args()

    if args.event_id < 0x8000 or args.event_id > 0xFFFE:
        raise ValueError("Event ID must be in the range [0x8000-0xFFFE]")

    (sd_trsp_u, sd_trsp_m, sd_prot, can_service) = asyncio.get_event_loop().run_until_complete(
        create_service(
            local_addr=args.local_addr,
            multicast_addr=args.multicast_addr,
            port=args.local_port,
            service_id=args.service_id,
            instance_id=args.instance_id,
            event_id=args.event_id,
            event_group_id=args.event_group_id,
        )
    )

    with can.interface.Bus(args.can_interface, bustype="socketcan") as can_bus:
        # The listener runs on another thread, so we need to explicitly pass the main thread's event
        # loop
        notifier = can.Notifier(can_bus, [CanListener(asyncio.get_event_loop(), can_service)])
        try:
            asyncio.get_event_loop().run_until_complete(
                run(sd_trsp_u, sd_trsp_m, sd_prot, can_service)
            )
        except KeyboardInterrupt:
            pass

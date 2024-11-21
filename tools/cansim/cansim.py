#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import datetime
import time

import canigen

parser = argparse.ArgumentParser(
    description="Generates SocketCAN messages for AWS IoT FleetWise demo"
)
parser.add_argument("-i", "--interface", default="vcan0", help="CAN interface, e.g. vcan0")
parser.add_argument("-o", "--only-obd", action="store_true", help="Only generate OBD messages")
args = parser.parse_args()

can_sim = canigen.Canigen(
    interface=args.interface,
    database_filename=None if args.only_obd else "hscan.dbc",
    obd_config_filename="obd_config.json",
)
NETWORK_TYPE = "NetworkType"
BRAKE_PRESSURE_SIGNAL = "DemoBrakePedalPressure"
ENGINE_TORQUE_SIGNAL = "DemoEngineTorque"


def set_with_print(func, name, val):
    print(str(datetime.datetime.now()), f"Set {name} to {val}")
    func(name, val)


def set_multiple_arguments_with_print(func, name, val1, val2):
    print(str(datetime.datetime.now()), f"Set {name} to {val1}: {val2}")
    func(name, val1, val2)


try:
    while True:
        set_with_print(can_sim.set_sig, BRAKE_PRESSURE_SIGNAL, 0)
        for i in range(1, 11):
            set_with_print(can_sim.set_pid, "ENGINE_SPEED", 1000 + i * 100)
            set_with_print(can_sim.set_pid, "VEHICLE_SPEED", i * 10)
            set_with_print(can_sim.set_pid, "FUEL_TANK_LEVEL", 100 - i)
            set_with_print(can_sim.set_pid, "AMBIENT_AIR_TEMPERATURE", 20 + i)
            set_with_print(can_sim.set_pid, "ENGINE_COOLANT_TEMPERATURE", 80 + i)
            set_with_print(can_sim.set_pid, "THROTTLE_POSITION", (i % 4) * 100)
            set_with_print(can_sim.set_dtc, "ECM_DTC1", i / 5 >= 1)
            set_with_print(can_sim.set_dtc, "ECM_DTC3", 0xAF)
            set_multiple_arguments_with_print(
                can_sim.set_dtc_snapshot, "ECM_DTC3", 1, ["0x01", "0xAA", "0xBB", hex(i)]
            )
            set_multiple_arguments_with_print(
                can_sim.set_dtc_ext_data, "ECM_DTC3", 1, ["0x01", "0xCC", "0xDD", hex(i * 2)]
            )
            time.sleep(5)
            if i < 6 or i > 9:
                # trigger is > 7000 so trigger
                set_with_print(can_sim.set_sig, BRAKE_PRESSURE_SIGNAL, 8000)
            set_with_print(can_sim.set_sig, ENGINE_TORQUE_SIGNAL, i * 100)
            time.sleep(0.5)
            set_with_print(can_sim.set_sig, BRAKE_PRESSURE_SIGNAL, i * 200)
            if i % 4 < 2:  # change network type every 10 seconds
                set_with_print(can_sim.set_sig, NETWORK_TYPE, 0)
            else:
                set_with_print(can_sim.set_sig, NETWORK_TYPE, 1)

except KeyboardInterrupt:
    print("Stopping...")
    can_sim.stop()
except Exception:
    raise

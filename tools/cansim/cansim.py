#!/usr/bin/python3
# Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
# SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
# Licensed under the Amazon Software License (the "License").
# You may not use this file except in compliance with the License.
# A copy of the License is located at
# http://aws.amazon.com/asl/
# or in the "license" file accompanying this file. This file is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
# express or implied. See the License for the specific language governing
# permissions and limitations under the License.

import canigen
import time
import datetime
import argparse

parser = argparse.ArgumentParser(description='Generates SocketCAN messages for AWS IoT FleetWise demo')
parser.add_argument('-i', '--interface', default='vcan0', help='CAN interface, e.g. vcan0')
parser.add_argument('-o', '--only-obd', action='store_true', help='Only generate OBD messages')
args = parser.parse_args()

can_sim = canigen.canigen(
    interface=args.interface,
    database_filename=None if args.only_obd else 'hscan.dbc',
    obd_config_filename='obd_config.json')
BRAKE_PRESSURE_SIGNAL='BrakePedalPressure'
ENGINE_TORQUE_SIGNAL='EngineTorque'

def set_with_print(func, name, val):
    print(str(datetime.datetime.now())+" Set "+name+" to "+str(val))
    func(name, val)

try:
    while True:
        set_with_print(can_sim.set_sig, BRAKE_PRESSURE_SIGNAL, 0)
        for i in range(1, 11):
            set_with_print(can_sim.set_pid, 'ENGINE_SPEED', 1000+i*100)
            set_with_print(can_sim.set_pid, 'VEHICLE_SPEED', i*10)
            set_with_print(can_sim.set_pid, 'FUEL_TANK_LEVEL', 100-i)
            set_with_print(can_sim.set_pid, 'AMBIENT_AIR_TEMPERATURE', 20+i)
            set_with_print(can_sim.set_pid, 'ENGINE_COOLANT_TEMPERATURE', 80+i)
            set_with_print(can_sim.set_pid, 'THROTTLE_POSITION', (i%4)*100)
            set_with_print(can_sim.set_dtc, 'ECM_DTC1', i/5 >= 1)
            time.sleep(5)
            if i<6 or i>9:
                # trigger is > 7000 so trigger
                set_with_print(can_sim.set_sig, BRAKE_PRESSURE_SIGNAL, 8000)
            set_with_print(can_sim.set_sig, ENGINE_TORQUE_SIGNAL, i*100)
            time.sleep(0.5)
            set_with_print(can_sim.set_sig, BRAKE_PRESSURE_SIGNAL, i*200)
except KeyboardInterrupt:
    print("Stopping...")
    can_sim.stop()
except:
    raise

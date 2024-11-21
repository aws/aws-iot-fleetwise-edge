#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import datetime
import time

import someipigen

domain = "local"
instance = "commonapi.ExampleSomeipInterface"
connection = "someipigen"
someip_sim = someipigen.SignalManager()
someip_sim.start(domain, instance, connection)

X_SIGNAL = "Vehicle.ExampleSomeipInterface.X"
A_SIGNAL = "Vehicle.ExampleSomeipInterface.A1.A2.A"
B_SIGNAL = "Vehicle.ExampleSomeipInterface.A1.A2.B"
D_SIGNAL = "Vehicle.ExampleSomeipInterface.A1.A2.D"


def set_with_print(func, name, val):
    print(str(datetime.datetime.now()) + " Set " + name + " to " + str(val))
    func(name, val)


try:
    while True:
        set_with_print(someip_sim.set_value, X_SIGNAL, 0)
        for i in range(1, 11):
            set_with_print(someip_sim.set_value, B_SIGNAL, i & 1 != 0)
            set_with_print(someip_sim.set_value, D_SIGNAL, i * 3.142)
            time.sleep(5)
            set_with_print(someip_sim.set_value, A_SIGNAL, i * 100)
            time.sleep(0.5)
            set_with_print(someip_sim.set_value, X_SIGNAL, i * 200)
except KeyboardInterrupt:
    print("Stopping...")
    someip_sim.stop()

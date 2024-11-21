#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import datetime
import time

import someip_device_shadow_editor

domain = "local"
instance = "commonapi.DeviceShadowOverSomeipInterface"
connection = "someip_device_shadow_editor"
someip_sim = someip_device_shadow_editor.DeviceShadowOverSomeipExampleApplication()
someip_sim.init(domain, instance, connection)


def call_get_shadow(shadow_name):
    print("==================================================")
    print(str(datetime.datetime.now()) + " Call get_shadow: shadowName=" + shadow_name)
    someip_sim.get_shadow(shadow_name)
    print("**************************************************")
    print("Sleeping for 1s")
    time.sleep(1)


def call_update_shadow(shadow_name, update_document):
    print("==================================================")
    print(
        str(datetime.datetime.now())
        + " Call update_shadow: shadowName="
        + shadow_name
        + "; update_document="
        + update_document
    )
    someip_sim.update_shadow(shadow_name, update_document)
    print("**************************************************")
    print("Sleeping for 1s")
    time.sleep(1)


def call_delete_shadow(shadow_name):
    print("==================================================")
    print(str(datetime.datetime.now()) + " Call delete_shadow: shadowName=" + shadow_name)
    someip_sim.delete_shadow(shadow_name)
    print("**************************************************")
    print("Sleeping for 1s")
    time.sleep(1)


try:
    time.sleep(10)
    call_get_shadow("")
    call_get_shadow("shadow-x")
    call_update_shadow("", "this string is not json compatible")
    call_update_shadow(
        "shadow-x",
        '{"state":{"desired":{"type":"classic shadow"},"reported":{"type":"???"}},"version":-1}',
    )
    call_get_shadow("")
    call_get_shadow("shadow-x")
    call_update_shadow(
        "", '{"state":{"desired":{"type":"classic shadow"},"reported":{"type":"???"}}}'
    )
    call_update_shadow(
        "shadow-x", '{"state":{"desired":{"type":"named shadow"},"reported":{"type":"???"}}}'
    )
    call_get_shadow("")
    call_get_shadow("shadow-x")
    call_delete_shadow("")
    call_delete_shadow("shadow-x")
    call_get_shadow("")
    call_get_shadow("shadow-x")
    call_update_shadow(
        "", '{"state":{"desired":{"type":"classic shadow"},"reported":{"type":"classic shadow"}}}'
    )
    call_update_shadow(
        "shadow-x",
        '{"state":{"desired":{"type":"named shadow"},"reported":{"type":"named shadow"}}}',
    )
except KeyboardInterrupt:
    print("Stopping...")
    someip_sim.deinit()

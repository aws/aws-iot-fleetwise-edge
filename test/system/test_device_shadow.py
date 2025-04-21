# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import logging
from pathlib import Path

import pytest
from testframework.context import Context

log = logging.getLogger(__name__)


class TestDeviceShadow:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int, someip_device_shadow_editor):
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            use_someip_device_shadow=True,
            someip_device_shadow_editor_instance=someip_device_shadow_editor.get_instance(),
        )

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_device_shadow(self, someip_device_shadow_editor, worker_number):
        self.context.start_fwe()

        new_shadow_document = '{"state":{"reported":{"temperature":22}}}'
        shadow_document_json = json.loads(new_shadow_document)

        # Setting initial shadow document
        someip_device_shadow_editor.update_shadow("", new_shadow_document)
        shadow_document = json.loads(someip_device_shadow_editor.get_shadow(""))
        assert shadow_document["state"] == shadow_document_json["state"]

        # Updating shadow document with desired state
        new_shadow_document = (
            '{"state":{"reported":{"temperature":22}, "desired":{"temperature":30}}}'
        )
        shadow_document_json = json.loads(new_shadow_document)
        someip_device_shadow_editor.update_shadow("", new_shadow_document)
        shadow_document = json.loads(someip_device_shadow_editor.get_shadow(""))
        # Expect delta field to be added and equal to the desired state
        assert shadow_document["state"]["delta"] == shadow_document_json["state"]["desired"]
        assert shadow_document["state"]["desired"] == shadow_document_json["state"]["desired"]
        assert shadow_document["state"]["reported"] == shadow_document_json["state"]["reported"]

        # Updating shadow document so the desired state is the same as reported state
        new_shadow_document = (
            '{"state":{"desired":{"temperature":30}, "reported":{"temperature":30}}}'
        )
        shadow_document_json = json.loads(new_shadow_document)
        someip_device_shadow_editor.update_shadow("", new_shadow_document)
        shadow_document = json.loads(someip_device_shadow_editor.get_shadow(""))
        assert shadow_document["state"] == shadow_document_json["state"]

        # Cleaning up shadow information
        someip_device_shadow_editor.delete_shadow("")

        # Setting initial named shadow document
        new_shadow_document = '{"state":{"reported":{"temperature":20}}}'
        shadow_document_json = json.loads(new_shadow_document)
        someip_device_shadow_editor.update_shadow("test-shadow", new_shadow_document)
        shadow_document = json.loads(someip_device_shadow_editor.get_shadow("test-shadow"))
        assert shadow_document["state"] == shadow_document_json["state"]

        # Updating named shadow document with desired state
        new_shadow_document = (
            '{"state":{"reported":{"temperature":20}, "desired":{"temperature":15}}}'
        )
        shadow_document_json = json.loads(new_shadow_document)
        someip_device_shadow_editor.update_shadow("test-shadow", new_shadow_document)
        shadow_document = json.loads(someip_device_shadow_editor.get_shadow("test-shadow"))
        # Expect delta field to be added and equal to the desired state
        assert shadow_document["state"]["delta"] == shadow_document_json["state"]["desired"]
        assert shadow_document["state"]["desired"] == shadow_document_json["state"]["desired"]
        assert shadow_document["state"]["reported"] == shadow_document_json["state"]["reported"]

        # Updating named shadow document so the desired state is the same as reported state
        new_shadow_document = (
            '{"state":{"desired":{"temperature":15}, "reported":{"temperature":15}}}'
        )
        shadow_document_json = json.loads(new_shadow_document)
        someip_device_shadow_editor.update_shadow("test-shadow", new_shadow_document)
        shadow_document = json.loads(someip_device_shadow_editor.get_shadow("test-shadow"))
        assert shadow_document["state"] == shadow_document_json["state"]

        # Cleaning up shadow information
        someip_device_shadow_editor.delete_shadow("test-shadow")

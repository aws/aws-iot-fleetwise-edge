# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import os
from pathlib import Path

from testframework.common import get_own_ip_address, is_hil

SOMEIP_MULTICAST_ADDRESS = "224.224.224.245"
SOMEIP_MULTICAST_PORT = "30490"
SERVICE_DISCOVERY_CONFIG = {
    "enable": "true",
    "multicast": SOMEIP_MULTICAST_ADDRESS,
    "port": SOMEIP_MULTICAST_PORT,
    "protocol": "udp",
    "initial_delay_min": "10",
    "initial_delay_max": "100",
    "repetitions_base_delay": "200",
    "repetitions_max": "3",
    "ttl": "3",
    "cyclic_offer_delay": "2000",
    "request_response_delay": "1500",
}
SOMEIP_SERVICE_ID_CAN = "0x7777"
LOGGING_CONFIG = {"level": "trace", "console": "true", "dlt": "false"}
LOCAL_VSOMEIP_CONFIG = {"logging": LOGGING_CONFIG}


def create_someipigen_config(tmp_path: Path, worker_number: int) -> str:
    if is_hil():
        someip_app_name_someipigen = "someipigen"
        someip_service_id_example = "0x1234"
        someip_instance_id_example = f"0x56{worker_number:02x}"
        someip_unicast_port_example = "30510"

        vsomeip_cfg_someipigen = {
            "unicast": get_own_ip_address(),
            "netmask": "255.255.0.0",
            "logging": LOGGING_CONFIG,
            "applications": [{"name": someip_app_name_someipigen, "id": "0x1414"}],
            "services": [
                {
                    "service": someip_service_id_example,
                    "instance": someip_instance_id_example,
                    "unreliable": someip_unicast_port_example,
                }
            ],
            "service-discovery": SERVICE_DISCOVERY_CONFIG,
        }
    else:
        vsomeip_cfg_someipigen = {"logging": LOGGING_CONFIG}

    vsomeip_cfg_file_someipigen = str(tmp_path / "vsomeip-someipigen.json")
    with open(vsomeip_cfg_file_someipigen, "w") as fp:
        fp.write(json.dumps(vsomeip_cfg_someipigen, indent=4))

    return vsomeip_cfg_file_someipigen


def create_device_someip_shadow_editor_config(tmp_path: Path, worker_number: int) -> str:
    if is_hil():
        someip_app_name_someip_device_shadow_editor = "someip_device_shadow_editor"

        vsomeip_cfg_someip_device_shadow_editor = {
            "unicast": get_own_ip_address(),
            "netmask": "255.255.0.0",
            "logging": LOGGING_CONFIG,
            "applications": [{"name": someip_app_name_someip_device_shadow_editor, "id": "0x1515"}],
            "service-discovery": SERVICE_DISCOVERY_CONFIG,
        }
    else:
        vsomeip_cfg_someip_device_shadow_editor = {"logging": LOGGING_CONFIG}

    vsomeip_cfg_file_someip_device_shadow_editor = str(
        tmp_path / "vsomeip-someip_device_shadow_editor.json"
    )
    with open(vsomeip_cfg_file_someip_device_shadow_editor, "w") as fp:
        fp.write(json.dumps(vsomeip_cfg_someip_device_shadow_editor, indent=4))

    return vsomeip_cfg_file_someip_device_shadow_editor


def create_can_to_someip_config(tmp_path: Path, someip_instance_id_can: str) -> str:
    vsomeip_cfg_can_to_someip = LOCAL_VSOMEIP_CONFIG
    if is_hil():
        someip_app_name_can_to_someip = "can-to-someip"
        someip_unicast_port_can = "30509"

        vsomeip_cfg_can_to_someip = {
            "unicast": get_own_ip_address(),
            "netmask": "255.255.0.0",
            "logging": LOGGING_CONFIG,
            "applications": [{"name": someip_app_name_can_to_someip, "id": "0x1212"}],
            "services": [
                {
                    "service": SOMEIP_SERVICE_ID_CAN,
                    "instance": someip_instance_id_can,
                    "unreliable": someip_unicast_port_can,
                }
            ],
            "service-discovery": SERVICE_DISCOVERY_CONFIG,
        }

    vsomeip_cfg_file_can_to_someip = str(tmp_path / "vsomeip-can-to-someip.json")
    with open(vsomeip_cfg_file_can_to_someip, "w") as fp:
        fp.write(json.dumps(vsomeip_cfg_can_to_someip, indent=4))

    return vsomeip_cfg_file_can_to_someip


def create_fwe_config(
    tmp_path: Path,
    someip_app_name_can_bridge,
    someip_app_name_collection,
    someip_app_name_command,
    someip_app_name_device_shadow,
    worker_number,
):
    vsomeip_cfg_fwe = LOCAL_VSOMEIP_CONFIG
    if is_hil():
        someip_service_id_device_shadow = "0x1235"
        someip_instance_id_device_shadow = f"0x57{worker_number:02x}"
        someip_unicast_port_device_shadow = "30511"

        vsomeip_cfg_fwe = {
            "unicast": os.environ["BOARD_IP_ADDRESS"],
            "netmask": "255.255.0.0",
            "logging": LOGGING_CONFIG,
            "applications": [
                {"name": someip_app_name_can_bridge, "id": "0x1313"},
                {"name": someip_app_name_command, "id": "0x1314"},
                {"name": someip_app_name_collection, "id": "0x1315"},
                {"name": someip_app_name_device_shadow, "id": "0x1316"},
            ],
            "services": [
                {
                    "service": someip_service_id_device_shadow,
                    "instance": someip_instance_id_device_shadow,
                    "unreliable": someip_unicast_port_device_shadow,
                }
            ],
            "service-discovery": SERVICE_DISCOVERY_CONFIG,
        }

    vsomeip_cfg_file_fwe = str(tmp_path / "vsomeip-fwe.json")
    with open(vsomeip_cfg_file_fwe, "w") as fp:
        fp.write(json.dumps(vsomeip_cfg_fwe, indent=4))

    return vsomeip_cfg_file_fwe

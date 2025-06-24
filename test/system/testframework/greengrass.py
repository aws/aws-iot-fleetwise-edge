# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import logging
import os
import subprocess
from pathlib import Path
from typing import Dict, List, Optional

import boto3
from tenacity import stop_after_delay
from testframework.aws_thing_creator import AwsThing
from testframework.common import Retrying
from testframework.network_namespace import NetworkNamespace
from testframework.process_utils import ProcessWrapper, SubprocessHelper

log = logging.getLogger(__name__)


class GreengrassComponentProcess(ProcessWrapper):
    def __init__(
        self,
        subprocess_helper: SubprocessHelper,
        greengrass_process: ProcessWrapper,
        args: str,
        executable: str,
        component_name: str,
        greengrass_cli_path: Path,
    ):
        self._subprocess_helper = subprocess_helper
        self._greengrass_process = greengrass_process
        self._component_name = component_name
        self._greengrass_cli_path = greengrass_cli_path

        pid = self._subprocess_helper.target_find_process_id_by_executable(
            executable=executable, ancestor=greengrass_process.pid
        )
        if pid is None:
            raise RuntimeError(
                f"Could not find a descendent of Greengrass process {self._greengrass_process.pid}"
                f" that matches the executable {executable}"
            )

        self.pid = pid

    def terminate(self):
        self._subprocess_helper.target_check_call(
            [
                str(self._greengrass_cli_path),
                "component",
                "stop",
                "--names",
                str(self._component_name),
            ]
        )

    def kill(self):
        self._subprocess_helper.target_check_call(["kill", "-s", "KILL", str(self.pid)])

    def wait(self, timeout: int) -> int:
        self._subprocess_helper.target_check_call(
            ["timeout", f"{timeout}s", "tail", "-f", "--pid", str(self.pid)]
        )
        # No straightforward way to get the exit code for a Greengrass component. So for now just
        # hard code a success.
        return 0


class GreengrassHelper:
    def __init__(
        self,
        tmp_path: Path,
        subprocess_helper: SubprocessHelper,
        greengrass_jar_path: Path,
        greengrass_nucleus_config_template_path: Path,
        aws_thing: AwsThing,
        fwe_recipe_template_path: Path,
        enable_aws_credentials: bool,
        network_namespace: Optional[NetworkNamespace] = None,
        keep_alive_interval_seconds: Optional[int] = None,
        ping_timeout_ms: Optional[int] = None,
        session_expiry_interval_seconds: Optional[int] = None,
    ):
        self._tmp_path = tmp_path
        self._subprocess_helper = subprocess_helper
        self._jar_path = greengrass_jar_path
        self._greengrass_nucleus_config_template_path = greengrass_nucleus_config_template_path
        self._aws_thing = aws_thing
        self._fwe_recipe_template_path = fwe_recipe_template_path
        self._enable_aws_credentials = enable_aws_credentials
        self._network_namespace = network_namespace

        # Use Greengrass' defaults:
        # https://docs.aws.amazon.com/greengrass/v2/developerguide/greengrass-nucleus-component.html
        self._keep_alive_interval_seconds = keep_alive_interval_seconds or 60
        self._ping_timeout_ms = ping_timeout_ms or 30000
        self._session_expiry_interval_seconds = session_expiry_interval_seconds or 604800  # 7 days

        self._root_path = tmp_path / "greengrass_v2_root"

        self._artifacts_path = tmp_path / "greengrass_v2_artifacts"
        self.logs_path = tmp_path / "greengrass_v2_logs"
        self._subprocess_helper.target_check_call(
            ["mkdir", "-p", str(self._artifacts_path), str(self.logs_path)]
        )

        self._recipes_path = tmp_path / "greengrass_v2_recipes"
        self._recipes_path.mkdir()

        self._component_user = self._subprocess_helper.target_check_output(["id", "-un"]).strip()
        self._aws_region: str = (
            boto3.DEFAULT_SESSION.region_name
            if boto3.DEFAULT_SESSION
            else boto3.Session().region_name
        )  # type: ignore

    def start(self):
        self._ipc_socket_dir_path = Path(
            self._subprocess_helper.target_check_output(
                ["mktemp", "-d", "-t", "greengrass.XXXXX"]
            ).strip()
        )
        self._ipc_socket_path = self._ipc_socket_dir_path / "ipc.socket"

        greengrass_config = (
            self._greengrass_nucleus_config_template_path.read_text()
            .replace("ROOT_PATH_PLACEHOLDER", str(self._root_path))
            .replace("IPC_SOCKET_PATH_PLACEHOLDER", str(self._ipc_socket_path))
            .replace("CERTIFICATE_FILENAME_PLACEHOLDER", str(self._aws_thing.cert_pem_path))
            .replace("PRIVATE_KEY_FILENAME_PLACEHOLDER", str(self._aws_thing.private_key_path))
            .replace("ROOT_CA_FILENAME_PLACEHOLDER", str(self._aws_thing.root_ca_path))
            .replace("THING_NAME_PLACEHOLDER", self._aws_thing.thing_name)
            .replace("AWS_REGION_PLACEHOLDER", self._aws_region)
            .replace(
                "IOT_ROLE_ALIAS_PLACEHOLDER",
                os.environ.get("TEST_ROLE_ALIAS", "ROLE_ALIAS_NOT_CONFIGURED"),
            )
            .replace("IOT_DATA_ENDPOINT_PLACEHOLDER", self._aws_thing.endpoint)
            .replace("IOT_CRED_ENDPOINT_PLACEHOLDER", self._aws_thing.creds_endpoint)
            .replace("LOG_DIR_PLACEHOLDER", str(self.logs_path))
            .replace(
                "KEEP_ALIVE_TIMEOUT_MS_PLACEHOLDER", str(self._keep_alive_interval_seconds * 1000)
            )
            .replace("PING_TIMEOUT_MS_PLACEHOLDER", str(self._ping_timeout_ms))
            .replace(
                "SESSION_EXPIRY_SECONDS_PLACEHOLDER", str(self._session_expiry_interval_seconds)
            )
        )

        self._greengrass_config_path = self._tmp_path / "greengrass_nucleus_config.yaml"
        self._greengrass_config_path.write_text(greengrass_config)
        self._subprocess_helper.copy_files_to_target([self._greengrass_config_path], self._tmp_path)

        self._greengrass_process = self._start_greengrass_process()

        greengrass_v2_client = boto3.client("greengrassv2")

        components = {
            # We need the CLI to be able to deploy FWE locally
            "aws.greengrass.Cli": {
                "componentVersion": "2.14.2",
            },
            # Nucleus is already part of the installation, but it is a dependency of other
            # components. This could make the Nucleus version to be updated, which would require
            # the Greengrass process to be restarted. So we just declare we want to deploy Nucleus
            # with the same version that is already installed to pin the version.
            "aws.greengrass.Nucleus": {
                "componentVersion": "2.14.2",
            },
        }
        if self._enable_aws_credentials:
            components.update(
                {
                    # We need this component to fetch AWS credentials
                    "aws.greengrass.TokenExchangeService": {
                        "componentVersion": "2.0.3",
                    },
                }
            )
        log.info(f"Creating deployment for components: {components}")
        response = greengrass_v2_client.create_deployment(
            targetArn=self._aws_thing.thing_arn, components=components
        )
        deployment_id = response["deploymentId"]

        for attempt in Retrying(stop=stop_after_delay(180)):
            with attempt:
                log.debug(f"Waiting for deployment to complete {deployment_id=}")
                response = greengrass_v2_client.list_effective_deployments(
                    coreDeviceThingName=self._aws_thing.thing_name,
                    maxResults=10,
                )
                assert "effectiveDeployments" in response
                deployments = [
                    deployment
                    for deployment in response["effectiveDeployments"]
                    if deployment["deploymentId"] == deployment_id
                ]
                assert len(deployments) == 1, f"No deployment found with id: {deployment_id}"
                assert "reason" in deployments[0]

                reason = deployments[0]["reason"]
                log.debug(f"Current deployment status reason: {reason}")
                assert reason == "SUCCESSFUL"

    def _start_greengrass_process(self) -> ProcessWrapper:
        return self._subprocess_helper.target_popen(
            [
                *(self._network_namespace.get_exec_list() if self._network_namespace else []),
                "java",
                "-Dlog.store=FILE",
                "-jar",
                str(self._jar_path),
                "--init-config",
                str(self._greengrass_config_path),
                "--component-default-user",
                f"{self._component_user}:{self._component_user}",
                "--setup-system-service",
                "false",
            ],
            executable="java",
        )

    def stop(self):
        self._greengrass_process.terminate()
        self._greengrass_process.wait(10)
        self._subprocess_helper.target_check_call(["rmdir", str(self._ipc_socket_dir_path)])

        # Greengrass changes the logs dir permissions to be accessible by the owner only, so make it
        # more permissible.
        self._subprocess_helper.target_check_call(["chmod", "755", str(self.logs_path)])

    def deploy_fwe(
        self, cmd_line_args: List[str], executable: str, extra_env: Dict[str, str]
    ) -> GreengrassComponentProcess:
        fwe_recipe = json.loads(self._fwe_recipe_template_path.read_text())
        fwe_recipe["Manifests"][0]["Lifecycle"]["Run"] = self._subprocess_helper.args_to_string(
            cmd_line_args
        )
        for env_var_name, env_var_value in extra_env.items():
            fwe_recipe["Manifests"][0]["Lifecycle"]["SetEnv"][env_var_name] = env_var_value
        fwe_recipe_path = self._recipes_path / "com.amazon.aws.IoTFleetWise-1.0.0.json"
        fwe_recipe_path.write_text(json.dumps(fwe_recipe, indent=2))
        self._subprocess_helper.copy_files_to_target([self._recipes_path], self._recipes_path)

        log.info("Deploying FWE as a Greengrass V2 component")
        greengrass_cli_path = self._root_path / "bin" / "greengrass-cli"
        self._subprocess_helper.target_check_call(
            [
                str(greengrass_cli_path),
                "deployment",
                "create",
                "--recipeDir",
                str(self._recipes_path),
                "--artifactDir",
                str(self._artifacts_path),
                "--merge",
                "com.amazon.aws.IoTFleetWise=1.0.0",
            ]
        )

        for attempt in Retrying(stop=stop_after_delay(60)):
            with attempt:
                log.debug("Waiting for FWE deployment to complete")
                try:
                    local_deployments = self._subprocess_helper.target_check_output(
                        [str(greengrass_cli_path), "deployment", "list"], stderr=subprocess.STDOUT
                    )
                except subprocess.CalledProcessError as e:
                    output = e.output.decode("utf-8") if e.output else ""
                    if "AccessDeniedException" in output:
                        raise AssertionError(
                            "Greengrass cli command failed to list deployments due to "
                            "AccessDeniedException. This is usually a transient issue."
                        )
                    raise

                log.debug(f"Local deployments output: {local_deployments}")
                status = [
                    line for line in local_deployments.splitlines() if line.startswith("Status: ")
                ]
                assert len(status) == 1
                status = status[0].replace("Status: ", "").strip()
                if status == "FAILED":
                    raise RuntimeError("Deployment failed")
                assert status == "SUCCEEDED"

        return GreengrassComponentProcess(
            self._subprocess_helper,
            greengrass_process=self._greengrass_process,
            args=self._subprocess_helper.args_to_string(cmd_line_args),
            executable=executable,
            component_name="com.amazon.aws.IoTFleetWise",
            greengrass_cli_path=greengrass_cli_path,
        )

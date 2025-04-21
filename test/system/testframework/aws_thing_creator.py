# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import configparser
import json
import logging
import os
from pathlib import Path
from typing import Optional

import boto3
from tenacity import retry_if_exception_type, stop_after_delay
from testframework.common import Retrying

log = logging.getLogger(__name__)

ROOT_CA = """
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
""".strip()


class AwsThing:
    def __init__(self, tmp_path: Path, name, s3_bucket_name: Optional[str], use_greengrass=False):
        self._tmp_path = tmp_path
        self._s3_bucket_name = s3_bucket_name
        self._use_greengrass = use_greengrass
        credentials_file = str(Path.home()) + "/.aws/credentials"
        if os.path.isfile(credentials_file):
            # Boto3 caches the contents of the credentials file. If the creds expire and the file
            # is updated with new ones, boto3 doesn't re-read it. Hence we manually read the file:
            config = configparser.RawConfigParser()
            config.read(credentials_file)
            self._iot_client = boto3.client(
                "iot",
                aws_access_key_id=config.get("default", "aws_access_key_id"),
                aws_secret_access_key=config.get("default", "aws_secret_access_key"),
                aws_session_token=config.get("default", "aws_session_token"),
            )
            self._iam_client = boto3.client(
                "iam",
                aws_access_key_id=config.get("default", "aws_access_key_id"),
                aws_secret_access_key=config.get("default", "aws_secret_access_key"),
                aws_session_token=config.get("default", "aws_session_token"),
            )
        else:
            self._iot_client = boto3.client("iot")
            self._iam_client = boto3.client("iam")
        self.thing_name = name
        self._policy_name = f"{self.thing_name}-policy"
        # This is an IAM role, which is limited to 64 chars, so we should try to make it as short
        # as possible.
        self._greengrass_token_exchange_role_name = f"{self.thing_name}-GG"
        self._greengrass_token_exchange_policy_name = (
            f"{self.thing_name}-GreengrassCoreTokenExchange"
        )
        self.greengrass_role_alias_name = f"{self.thing_name}-GreengrassCoreTokenExchangeRoleAlias"
        self._greengrass_role_alias_policy_name = (
            f"{self.thing_name}-GreengrassCoreTokenExchangeRoleAliasPolicy"
        )
        self._greengrass_core_policy_name = f"{self.thing_name}-GreengrassCorePolicy"

        self._create_thing()

    def _create_thing(self):
        log.info("Creating thing %s..." % self.thing_name)
        response = self._iot_client.create_thing(thingName=self.thing_name)
        self.thing_arn = response["thingArn"]
        response = self._iot_client.create_keys_and_certificate(setAsActive=True)
        self._cert_id = response["certificateId"]
        self._cert_arn = response["certificateArn"]
        self.cert_pem = response["certificatePem"]
        self.private_key = response["keyPair"]["PrivateKey"]

        self.cert_pem_path = self._tmp_path / f"{self.thing_name}-certificate.crt"
        self.cert_pem_path.write_text(self.cert_pem)
        self.private_key_path = self._tmp_path / f"{self.thing_name}-private_key.pem"
        self.private_key_path.write_text(self.private_key)
        self.root_ca_path = self._tmp_path / "AmazonRootCA1.pem"
        self.root_ca_path.write_text(ROOT_CA)

        self.endpoint = self._iot_client.describe_endpoint(endpointType="iot:Data-ATS")[
            "endpointAddress"
        ]
        self.creds_endpoint = self._iot_client.describe_endpoint(
            endpointType="iot:CredentialProvider"
        )["endpointAddress"]
        response = self._iot_client.create_policy(
            policyName=self._policy_name,
            policyDocument=json.dumps(
                {
                    "Version": "2012-10-17",
                    "Statement": [{"Effect": "Allow", "Action": ["iot:*"], "Resource": ["*"]}],
                }
            ),
        )
        response = self._iot_client.attach_policy(
            policyName=self._policy_name, target=self._cert_arn
        )

        if self._use_greengrass:
            response = self._iam_client.create_role(
                RoleName=self._greengrass_token_exchange_role_name,
                AssumeRolePolicyDocument=json.dumps(
                    {
                        "Version": "2012-10-17",
                        "Statement": [
                            {
                                "Effect": "Allow",
                                "Principal": {"Service": "credentials.iot.amazonaws.com"},
                                "Action": "sts:AssumeRole",
                            }
                        ],
                    }
                ),
            )
            role_arn = response["Role"]["Arn"]
            policy_document = {
                "Version": "2012-10-17",
                "Statement": [
                    {
                        "Effect": "Allow",
                        "Action": [
                            "logs:CreateLogGroup",
                            "logs:CreateLogStream",
                            "logs:PutLogEvents",
                            "logs:DescribeLogStreams",
                            "s3:GetBucketLocation",
                        ],
                        "Resource": "*",
                    }
                ],
            }
            if self._s3_bucket_name:
                # The token exchange role is offered by Greengrass' TokenExchangeService component
                # and it will be assumed by FWE when creating AWS clients.
                # So we need to add the needed S3 permissions for FWE to upload data.
                policy_document["Statement"].append(
                    {
                        "Effect": "Allow",
                        "Action": [
                            "s3:ListBucket",
                            "s3:PutObject",
                            "s3:PutObjectAcl",
                        ],
                        "Resource": [
                            f"arn:aws:s3:::{self._s3_bucket_name}",
                            (
                                f"arn:aws:s3:::{self._s3_bucket_name}"
                                "/*raw-data/${credentials-iot:ThingName}/*"
                            ),
                        ],
                    }
                )

            response = self._iam_client.put_role_policy(
                RoleName=self._greengrass_token_exchange_role_name,
                PolicyName=self._greengrass_token_exchange_policy_name,
                PolicyDocument=json.dumps(policy_document),
            )

            response = self._iot_client.create_role_alias(
                roleAlias=self.greengrass_role_alias_name,
                roleArn=role_arn,
            )
            role_alias_arn = response["roleAliasArn"]
            response = self._iot_client.create_policy(
                policyName=self._greengrass_role_alias_policy_name,
                policyDocument=json.dumps(
                    {
                        "Version": "2012-10-17",
                        "Statement": [
                            {
                                "Effect": "Allow",
                                "Action": "iot:AssumeRoleWithCertificate",
                                "Resource": role_alias_arn,
                            }
                        ],
                    }
                ),
            )
            response = self._iot_client.create_policy(
                policyName=self._greengrass_core_policy_name,
                policyDocument=json.dumps(
                    {
                        "Version": "2012-10-17",
                        "Statement": [
                            {"Effect": "Allow", "Action": "greengrass:*", "Resource": "*"}
                        ],
                    }
                ),
            )
            response = self._iot_client.attach_policy(
                policyName=self._greengrass_role_alias_policy_name,
                target=self._cert_arn,
            )
            response = self._iot_client.attach_policy(
                policyName=self._greengrass_core_policy_name,
                target=self._cert_arn,
            )

        response = self._iot_client.attach_thing_principal(
            thingName=self.thing_name, principal=self._cert_arn
        )
        log.info("Created thing")

    def _delete_thing(self):
        log.info("Deleting thing %s..." % self.thing_name)
        response = self._iot_client.list_thing_principals(thingName=self.thing_name)
        for i in response["principals"]:
            response = self._iot_client.detach_thing_principal(
                thingName=self.thing_name, principal=i
            )
            response = self._iot_client.detach_policy(policyName=self._policy_name, target=i)

            if self._use_greengrass:
                response = self._iot_client.detach_policy(
                    policyName=self._greengrass_role_alias_policy_name,
                    target=i,
                )
                response = self._iot_client.detach_policy(
                    policyName=self._greengrass_core_policy_name,
                    target=i,
                )

            response = self._iot_client.update_certificate(
                certificateId=i.split("/")[-1], newStatus="INACTIVE"
            )
            response = self._iot_client.delete_certificate(
                certificateId=i.split("/")[-1], forceDelete=True
            )
            response = self._iot_client.delete_thing(thingName=self.thing_name)

        self._delete_iot_policy(self._policy_name)
        if self._use_greengrass:
            self._delete_iot_policy(self._greengrass_role_alias_policy_name)
            self._delete_iot_policy(self._greengrass_core_policy_name)

            log.info(f"Deleting IoT role alias '{self.greengrass_role_alias_name}'")
            response = self._iot_client.delete_role_alias(roleAlias=self.greengrass_role_alias_name)
            log.info(f"Deleting IAM role policy'{self._greengrass_token_exchange_role_name}'")
            response = self._iam_client.delete_role_policy(
                RoleName=self._greengrass_token_exchange_role_name,
                PolicyName=self._greengrass_token_exchange_policy_name,
            )
            log.info(f"Deleting IAM role '{self._greengrass_token_exchange_role_name}'")
            response = self._iam_client.delete_role(
                RoleName=self._greengrass_token_exchange_role_name
            )
        log.info("Deleted thing")

    def _delete_iot_policy(self, policy_name):
        log.info(f"Deleting IoT policy '{policy_name}'")
        # The detach_policy operation can take several minutes to propagate, so we need to retry
        # when deleting the policy.
        # See: https://boto3.amazonaws.com/v1/documentation/api/1.26.82/reference/services/iot/client/detach_policy.html  # noqa: E501
        for attempt in Retrying(
            wait=stop_after_delay(300),
            retry=retry_if_exception_type(self._iot_client.exceptions.DeleteConflictException),
        ):
            with attempt:
                self._iot_client.delete_policy(policyName=policy_name)

    def stop(self):
        self._delete_thing()

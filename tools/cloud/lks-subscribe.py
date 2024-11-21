#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import random
import time
from concurrent.futures import Future

import boto3
from awscrt import auth, mqtt5
from awsiot import mqtt5_client_builder
from botocore.config import Config
from google.protobuf.json_format import MessageToJson

received_data = []


def on_publish_received(message):
    import last_known_state_message_pb2

    global received_data
    packet = message.publish_packet
    print(f"Received message on topic: {packet.topic}")
    try:
        msg = last_known_state_message_pb2.LastKnownState()
        msg.ParseFromString(packet.payload)
        json_msg = MessageToJson(msg)
        print(f"Received message: {json_msg}")
        received_data += [json.loads(json_msg)]
    except Exception as e:
        print(f"Error parsing message: {e}")


def main():
    parser = argparse.ArgumentParser(
        description="Receives Last Known State data on the customer MQTT topic"
    )
    parser.add_argument("--endpoint-url", metavar="URL", help="IoT Core endpoint URL", default=None)
    parser.add_argument("--region", metavar="REGION", help="IoT Core region", default="us-east-1")
    parser.add_argument(
        "--client-id",
        metavar="ID",
        help="Client ID for the MQTT connection. If omitted, a random one will be generated.",
        default=f"lks-subscribe-script-{random.randint(0, 2**32 - 1):08x}",
    )
    parser.add_argument("--run-time", metavar="SEC", help="Run time, zero is infinite", default="0")
    parser.add_argument("--output-file", metavar="FILE", help="Output JSON file", default=None)
    parser.add_argument(
        "--iotfleetwise-topic-prefix",
        metavar="PREFIX",
        help="MQTT topic prefix",
        default="$aws/iotfleetwise/",
    )
    parser.add_argument("--vehicle-name", metavar="NAME", help="Vehicle name", required=True)
    parser.add_argument(
        "--template-name", metavar="NAME", help="State template name", required=True
    )
    args = parser.parse_args()

    session = boto3.Session()
    iot_client = session.client(
        "iot", endpoint_url=args.endpoint_url, config=Config(region_name=args.region)
    )
    iot_endpoint = iot_client.describe_endpoint(endpointType="iot:Data-ATS")["endpointAddress"]
    credentials_provider = auth.AwsCredentialsProvider.new_default_chain()

    stop_future = Future()

    def on_lifecycle_connection_success(data: mqtt5.LifecycleConnectSuccessData):
        print(f"MQTT connection succeeded {data=}")

    def on_lifecycle_connection_failure(data: mqtt5.LifecycleConnectFailureData):
        print(f"MQTT connection failed {data=}")

    def on_lifecycle_disconnection(data: mqtt5.LifecycleDisconnectData):
        print(f"MQTT disconnected {data=}")

    def on_lifecycle_stopped(data: mqtt5.LifecycleStoppedData):
        print(f"MQTT client stopped {data=}")
        stop_future.set_result(None)

    mqtt_connection = mqtt5_client_builder.websockets_with_default_aws_signing(
        endpoint=iot_endpoint,
        region=args.region,
        on_publish_received=on_publish_received,
        on_lifecycle_stopped=on_lifecycle_stopped,
        on_lifecycle_connection_success=on_lifecycle_connection_success,
        on_lifecycle_connection_failure=on_lifecycle_connection_failure,
        on_lifecycle_disconnection=on_lifecycle_disconnection,
        credentials_provider=credentials_provider,
        client_id=args.client_id,
        clean_session=False,
        keep_alive_secs=30,
    )
    mqtt_connection.start()
    mqtt_topic = (
        f"{args.iotfleetwise_topic_prefix}vehicles/{args.vehicle_name}/"
        f"last_known_state/{args.template_name}/data"
    )
    subscribe_future = mqtt_connection.subscribe(
        subscribe_packet=mqtt5.SubscribePacket(
            subscriptions=[mqtt5.Subscription(topic_filter=mqtt_topic, qos=mqtt5.QoS.AT_LEAST_ONCE)]
        )
    )
    subscribe_res = subscribe_future.result(1000)
    print(f"Established mqtt subscription to {mqtt_topic} with {subscribe_res.reason_codes}")
    run_time = 0
    try:
        while int(args.run_time) == 0 or run_time < int(args.run_time):
            time.sleep(1)
            run_time += 1
    except KeyboardInterrupt:
        pass

    mqtt_connection.stop()
    stop_future.result(timeout=10)

    if args.output_file:
        with open(args.output_file, "w") as fp:
            fp.write(json.dumps(received_data, indent=2))


if __name__ == "__main__":
    main()

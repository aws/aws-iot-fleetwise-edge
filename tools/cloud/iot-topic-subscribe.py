#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import time
from concurrent.futures import Future

import boto3
from awscrt import auth, mqtt5
from awsiot import mqtt5_client_builder
from botocore.config import Config

received_data = None


def on_publish_received(message):
    global received_data
    packet = message.publish_packet
    print(f"Received message on topic: {packet.topic}")
    try:
        # Parse the message payload and extract the relevant data
        data = json.loads(packet.payload)
        if received_data is None:
            print(f"Received data: {json.dumps(data, indent=2)}")
        else:
            received_data.append(data)
    except Exception as e:
        print(f"Error parsing message: {e}")


def main():
    parser = argparse.ArgumentParser(description="Receives data from the IoT topic")
    parser.add_argument("--endpoint-url", metavar="URL", help="IoT Core endpoint URL", default=None)
    parser.add_argument("--region", metavar="REGION", help="IoT Core region", default="us-east-1")
    parser.add_argument("--client-id", metavar="ID", help="IoT Core region", default="CUSTOMER_APP")
    parser.add_argument("--run-time", metavar="SEC", help="Run time, zero is infinite", default="0")
    parser.add_argument("--output-file", metavar="FILE", help="Output JSON file", default=None)
    parser.add_argument("--iot-topic", metavar="IOT_TOPIC", help="MQTT topic", required=True)
    parser.add_argument("--vehicle-name", metavar="NAME", help="Vehicle name", required=True)
    args = parser.parse_args()

    if args.output_file:
        global received_data
        received_data = []

    session = boto3.Session()
    iot_client = session.client(
        "iot", endpoint_url=args.endpoint_url, config=Config(region_name=args.region)
    )
    iot_endpoint = iot_client.describe_endpoint(endpointType="iot:Data-ATS")["endpointAddress"]
    credentials_provider = auth.AwsCredentialsProvider.new_default_chain()

    stop_future = Future()

    def on_lifecycle_stopped(data: mqtt5.LifecycleStoppedData):
        print(f"MQTT client stopped {data=}")
        stop_future.set_result(None)

    mqtt_connection = mqtt5_client_builder.websockets_with_default_aws_signing(
        endpoint=iot_endpoint,
        region=args.region,
        on_publish_received=on_publish_received,
        on_lifecycle_stopped=on_lifecycle_stopped,
        credentials_provider=credentials_provider,
        client_id=args.client_id,
        clean_session=False,
        keep_alive_secs=30,
    )
    mqtt_connection.start()
    mqtt_topic = f"{args.iot_topic}"
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

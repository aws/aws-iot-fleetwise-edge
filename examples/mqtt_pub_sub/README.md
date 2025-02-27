# MQTT pub/sub example

This example configures an extra MQTT subscription to the `ping` topic and responds to the `pong`
topic.

This guide assumes you have already configured your development machine and built the examples using
[these](../README.md#building) instructions.

## Provisioning and Configuration

Run the following to provision credentials for the vehicle:

```bash
cd ~/aws-iot-fleetwise-edge/examples/mqtt_pub_sub \
&& mkdir -p build_config \
&& ../../tools/provision.sh \
    --region us-east-1 \
    --vehicle-name fwe-example-mqtt-pub-sub \
    --certificate-pem-outfile build_config/certificate.pem \
    --private-key-outfile build_config/private-key.key \
    --endpoint-url-outfile build_config/endpoint.txt \
    --vehicle-name-outfile build_config/vehicle-name.txt \
&& ../../tools/configure-fwe.sh \
    --input-config-file ../../configuration/static-config.json \
    --output-config-file build_config/config-0.json \
    --log-color Yes \
    --log-level Trace \
    --vehicle-name `cat build_config/vehicle-name.txt` \
    --endpoint-url `cat build_config/endpoint.txt` \
    --certificate-file `realpath build_config/certificate.pem` \
    --private-key-file `realpath build_config/private-key.key` \
    --persistency-path `realpath build_config`
```

## Running the example

1. Run the example with the config file:

   ```bash
   ../build/mqtt_pub_sub/fwe-example-mqtt-pub-sub \
       build_config/config-0.json
   ```

1. Go to the IoT Core web test client:
   https://us-east-1.console.aws.amazon.com/iot/home?region=us-east-1#/test

1. Subscribe to the `pong` topic, then publish a message to the `ping` topic.

1. In the example log you will see:

   ```
   [TRACE] [AwsIotConnectivityModule.cpp:339] [operator()()]: [Data received on the topic: ping with a payload length of: 4]
   [INFO ] [main.cpp:57] [operator()()]: [Received message on topic ping with payload <MESSAGE_PAYLOAD>. Sending pong...]
   [TRACE] [AwsIotSender.cpp:137] [operator()()]: [Publish succeeded]
   [INFO ] [main.cpp:69] [operator()()]: [Pong sent successfully]
   ```

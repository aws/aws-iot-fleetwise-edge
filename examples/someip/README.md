# SOME/IP example

<!-- prettier-ignore -->
> [!NOTE]
> This guide makes use of "gated" features of AWS IoT FleetWise for which you will need to request
> access. See
> [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html) for
> more information, or contact the
> [AWS Support Center](https://console.aws.amazon.com/support/home#/).

This example uses custom FIDL and FDEPL files
[`MySomeipInterface.fidl`](./fidl/MySomeipInterface.fidl) and
[`MySomeipInterface.fdepl`](./fidl/MySomeipInterface.fdepl) for data collection.

This guide assumes you have already configured your development machine and built the examples using
[these](../README.md#building) instructions.

## Provisioning and Configuration

Run the following to provision credentials for the vehicle:

```bash
cd ~/aws-iot-fleetwise-edge/examples/someip \
&& mkdir -p build_config \
&& ../../tools/provision.sh \
    --region us-east-1 \
    --vehicle-name fwe-example-someip \
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
    --persistency-path `realpath build_config` \
&& MY_SOMEIP_INTERFACE=`echo {} | jq ".interfaceId=\"MY_SOMEIP\"" \
    | jq ".type=\"mySomeipCollectionInterface\"" \
    | jq ".mySomeipCollectionInterface.someipApplicationName=\"mySomeipCollectionInterface\"" \
    | jq ".mySomeipCollectionInterface.someipInstance=\"commonapi.MySomeipInterface\"" \
    | jq ".mySomeipCollectionInterface.cyclicUpdatePeriodMs=500"` \
&& OUTPUT_CONFIG=`jq ".networkInterfaces+=[${MY_SOMEIP_INTERFACE}]" build_config/config-0.json` \
&& echo "${OUTPUT_CONFIG}" > build_config/config-0.json
```

## Running the example

1. Run the example with the config file:

   ```bash
   ../build/someip/fwe-example-someip \
       build_config/config-0.json
   ```

1. Open a new terminal _on the development machine_ and download and run the SOME/IP simulator from
   GitHub. (Alternatively you could build the `someipigen.so` file from source by building FWE again
   as an executable with the CMake option `-DFWE_BUILD_EXECUTABLE=On`.) Note: This simulator also
   works with the example FIDL and FDEPL files in this folder because they use the same identifiers
   and types as the FWE example FIDL and FDEPL files:
   [`ExampleSomeipInterface.fidl`](../../interfaces/someip/fidl/ExampleSomeipInterface.fidl) and
   [`ExampleSomeipInterface.fdepl`](../../interfaces/someip/fidl/ExampleSomeipInterface.fdepl).

   1. If your development machine is ARM64 (the default if you launched an EC2 instance using the
      CloudFormation template):

      ```bash
      cd ~/aws-iot-fleetwise-edge \
      && mkdir -p build \
      && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
          https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-arm64.tar.gz  \
      && tar -zxf build/aws-iot-fleetwise-edge.tar.gz tools/someipigen/someipigen.so \
      && cd tools/someipigen \
      && python3 someipsim.py
      ```

   1. If your development machine is x86_64:

      ```bash
      cd ~/aws-iot-fleetwise-edge \
      && mkdir -p build \
      && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
          https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-amd64.tar.gz  \
      && tar -zxf build/aws-iot-fleetwise-edge.tar.gz tools/someipigen/someipigen.so \
      && cd tools/someipigen \
      && python3 someipsim.py
      ```

1. Open a new terminal _on the development machine_ and run the following to install the
   dependencies of the demo script:

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
   && sudo -H ./install-deps.sh
   ```

1. Run the demo script:

   ```bash
   ./demo.sh \
       --region us-east-1 \
       --vehicle-name fwe-example-someip \
       --node-file ../../examples/someip/custom-nodes-my-someip.json \
       --decoder-file ../../examples/someip/custom-decoders-my-someip.json \
       --network-interface-file ../../examples/someip/network-interface-custom-my-someip.json \
       --campaign-file ../../examples/someip/campaign-my-someip-heartbeat.json \
       --data-destination IOT_TOPIC
   ```

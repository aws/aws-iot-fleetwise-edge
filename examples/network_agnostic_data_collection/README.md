# Network agnostic data collection example

<!-- prettier-ignore -->
> [!NOTE]
> This is a "gated" feature of AWS IoT FleetWise for which you will need to request access. See
> [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html)
> for more information, or contact the
> [AWS Support Center](https://console.aws.amazon.com/support/home#/).

This example configures three network interfaces for data collection, using the
[Network agnostic](../../docs/dev-guide/network-agnostic-dev-guide.md#sensor-data-collection)
approach:

1. The [`NamedSignalDataSource`](../../include/aws/iotfleetwise/NamedSignalDataSource.h) is used to
   ingest random location data to the signals `Vehicle.CurrentLocation.Latitude` and
   `Vehicle.CurrentLocation.Longitude`.
1. The [`MyCounterDataSource`](./MyCustomDataSource.h) is used to ingest a string signal
   `Vehicle.MyCounter` with value `"Hello world! <COUNTER>"`. This approach is needed when
   additional configuration parameters are required, in this case the parameter `myOption1`.
1. The [`MyCustomDataSource`](./MyCustomDataSource.h) uses the decoder string as a CSV containing a
   `key` and two additional example parameters: `exampleParam1ByteOffset` and
   `exampleParam2ScalingFactor`, and ingests the value 42 on any signal configured for that
   interface.

Also refer to the
[cloud developer guide](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/network-agnostic-data-collection.html).

This guide assumes you have already configured your development machine and built the examples using
[these](../README.md#building) instructions.

## Provisioning and Configuration

Run the following to provision credentials for the vehicle:

```bash
cd ~/aws-iot-fleetwise-edge/examples/network_agnostic_data_collection \
&& mkdir -p build_config \
&& ../../tools/provision.sh \
    --region us-east-1 \
    --vehicle-name fwe-example-network-agnostic-data-collection \
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
    --enable-named-signal-interface \
&& MY_COUNTER_INTERFACE=`echo {} | jq ".interfaceId=\"MY_COUNTER\"" \
    | jq ".type=\"myCounterInterface\"" \
    | jq ".myCounterInterface.myOption1=10"` \
&& MY_CUSTOM_INTERFACE=`echo {} | jq ".interfaceId=\"MY_CUSTOM\"" \
    | jq ".type=\"myCustomInterface\""` \
&& OUTPUT_CONFIG=`cat build_config/config-0.json \
    | jq ".networkInterfaces+=[${MY_COUNTER_INTERFACE}]" \
    | jq ".networkInterfaces+=[${MY_CUSTOM_INTERFACE}]"` \
&& echo "${OUTPUT_CONFIG}" > build_config/config-0.json
```

## Running the example

1. Run the example with the config file:

   ```bash
   ../build/network_agnostic_data_collection/fwe-example-network-agnostic-data-collection \
       build_config/config-0.json
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
       --vehicle-name fwe-example-network-agnostic-data-collection \
       --node-file custom-nodes-location.json \
       --decoder-file ../../examples/network_agnostic_data_collection/custom-decoders-my-location.json \
       --network-interface-file network-interface-custom-named-signal.json \
       --node-file ../../examples/network_agnostic_data_collection/custom-nodes-my-counter.json \
       --decoder-file ../../examples/network_agnostic_data_collection/custom-decoders-my-counter.json \
       --network-interface-file ../../examples/network_agnostic_data_collection/network-interface-custom-my-counter.json \
       --node-file ../../examples/network_agnostic_data_collection/custom-nodes-my-custom.json \
       --decoder-file ../../examples/network_agnostic_data_collection/custom-decoders-my-custom.json \
       --network-interface-file ../../examples/network_agnostic_data_collection/network-interface-custom-my-custom.json \
       --campaign-file ../../examples/network_agnostic_data_collection/campaign-nadc-example-heartbeat.json \
       --data-destination IOT_TOPIC
   ```

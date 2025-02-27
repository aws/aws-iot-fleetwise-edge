# Custom function example

<!-- prettier-ignore -->
> [!NOTE]
> This is a "gated" feature of AWS IoT FleetWise for which you will need to request access. See
> [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html)
> for more information, or contact the
> [AWS Support Center](https://console.aws.amazon.com/support/home#/).

This example configures several custom functions, as documented in the
[custom function guide](../../docs/dev-guide/custom-function-dev-guide.md#developing-your-own-custom-functions).

This guide assumes you have already configured your development machine and built the examples using
[these](../README.md#building) instructions.

## Provisioning and Configuration

Run the following to provision credentials for the vehicle and configure CAN data collection:

```bash
cd ~/aws-iot-fleetwise-edge/examples/custom_function \
&& mkdir -p build_config \
&& ../../tools/provision.sh \
    --region us-east-1 \
    --vehicle-name fwe-example-custom-function \
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
    --can-bus0 vcan0 \
    --enable-named-signal-interface
```

## Running the example

1. Run the example with the config file:

   ```bash
   ../build/custom_function/fwe-example-custom-function \
       build_config/config-0.json
   ```

1. Open a new terminal _on the development machine_ and start the CAN simulator:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && cd tools/cansim \
   && python3 cansim.py
   ```

1. Open a new terminal _on the development machine_ and run the following to install the
   dependencies of the demo script:

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
   && sudo -H ./install-deps.sh

   ```

1. Run the following command to generate 'node' and 'decoder' JSON files from the input DBC file:

   ```bash
   python3 dbc-to-nodes.py hscan.dbc can-nodes.json \
   && python3 dbc-to-decoders.py hscan.dbc can-decoders.json
   ```

1. Deploy a campaign that uses the `sin` custom function, see
   [`campaign-sin.json`](./campaign-sin.json):

   ```bash
   ./demo.sh \
       --region us-east-1 \
       --vehicle-name fwe-example-custom-function \
       --node-file can-nodes.json \
       --decoder-file can-decoders.json \
       --network-interface-file network-interface-can.json \
       --campaign-file ../../examples/custom_function/campaign-sin.json \
       --data-destination IOT_TOPIC
   ```

1. Deploy a campaign that uses the `file_size` custom function, see
   [`campaign-file-size.json`](./campaign-file-size.json):

   ```bash
   ./demo.sh \
       --region us-east-1 \
       --vehicle-name fwe-example-custom-function \
       --node-file ../../examples/custom_function/custom-nodes-file-size.json \
       --decoder-file ../../examples/custom_function/custom-decoders-file-size.json \
       --network-interface-file network-interface-custom-named-signal.json \
       --campaign-file ../../examples/custom_function/campaign-file-size.json \
       --data-destination IOT_TOPIC
   ```

   1. Trigger the campaign by creating a file larger than 10 bytes at the location specified in the
      campaign file:

      ```bash
      echo "Hello world!" > /tmp/fwe_test_file
      ```

1. Deploy a campaign that uses the `counter` custom function, see
   [`campaign-counter.json`](./campaign-counter.json):

   ```bash
   ./demo.sh \
       --region us-east-1 \
       --vehicle-name fwe-example-custom-function \
       --node-file can-nodes.json \
       --decoder-file can-decoders.json \
       --network-interface-file network-interface-can.json \
       --campaign-file ../../examples/custom_function/campaign-counter.json \
       --data-destination IOT_TOPIC
   ```

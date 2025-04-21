# Examples of using FWE as an external library

The Reference Implementation for AWS IoT FleetWise (FWE) can be compiled as a library, which can
then be used as a C++ dependency in external projects.

If you are only interested in using FWE as an executable, rather than as an external library, refer
to the [Edge Agent Developer Guide](../docs/dev-guide/edge-agent-dev-guide.md).

<!-- prettier-ignore -->
> [!NOTE]
> The FWE C++ API is currently **not** guaranteed to remain stable between versions. Therefore if
> you update the FWE library, you must also update your consuming projects, and update the usage
> of the FWE C++ APIs according to the changed interfaces.

## Building

### Prerequisites

- Access to an AWS Account with administrator privileges.
- Your AWS account has access to AWS IoT FleetWise "gated" features. See
  [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html) for
  more information, or contact the
  [AWS Support Center](https://console.aws.amazon.com/support/home#/).
- Logged in to the AWS Console in the `us-east-1` region using the account with administrator
  privileges.
  - Note: if you would like to use a different region you will need to change `us-east-1` to your
    desired region in each place that it is mentioned below.
  - Note: AWS IoT FleetWise is currently available in
    [these](https://docs.aws.amazon.com/general/latest/gr/iotfleetwise.html) regions.
- A local Linux or MacOS machine.

### Launch your development machine

An Ubuntu 22.04 development machine with 200GB free disk space will be required. A local Intel
x86_64 (amd64) machine can be used, however it is recommended to use the following instructions to
launch an AWS EC2 Graviton (arm64) instance. Pricing for EC2 can be found,
[here](https://aws.amazon.com/ec2/pricing/on-demand/).

1. Launch an EC2 Graviton instance with administrator permissions:
   [**Launch CloudFormation Template**](https://us-east-1.console.aws.amazon.com/cloudformation/home?region=us-east-1#/stacks/quickcreate?templateUrl=https%3A%2F%2Faws-iot-fleetwise.s3.us-west-2.amazonaws.com%2Flatest%2Fcfn-templates%2Ffwdev.yml&stackName=fwdev).
1. Enter the **Name** of an existing SSH key pair in your account from
   [here](https://us-east-1.console.aws.amazon.com/ec2/v2/home?region=us-east-1#KeyPairs:).
   1. Do not include the file suffix `.pem`.
   1. If you do not have an SSH key pair, you will need to create one and download the corresponding
      `.pem` file. Be sure to update the file permissions: `chmod 400 <PATH_TO_PEM>`
1. **Select the checkbox** next to _'I acknowledge that AWS CloudFormation might create IAM
   resources with custom names.'_
1. Choose **Create stack**.
1. Wait until the status of the Stack is **CREATE_COMPLETE**; this can take up to five minutes.
1. Select the **Outputs** tab, copy the EC2 IP address, and connect via SSH from your local machine
   to the development machine.

   ```bash
   ssh -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>
   ```

## Obtain the FWE code

1. Run the following _on the development machine_ to clone the latest FWE source code from GitHub.

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge
   ```

## Build and install FWE as a library

1. Install the FWE dependencies, including for SOME/IP support:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && ./tools/install-deps-native.sh \
     --with-someip-support
   ```

1. Build and install FWE as a library with support for S3 and remote commands:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && mkdir -p build && cd build \
   && cmake \
     -DFWE_STATIC_LINK=On \
     -DFWE_BUILD_EXECUTABLE=Off \
     -DBUILD_TESTING=Off \
     -DFWE_FEATURE_S3=On \
     -DFWE_FEATURE_REMOTE_COMMANDS=On \
     .. \
   && make -j`nproc` \
   && sudo make install
   ```

### Build the examples

1. Build all the examples using FWE as an external library:

   ```bash
   cd ~/aws-iot-fleetwise-edge/examples \
   && mkdir -p build && cd build \
   && cmake .. \
   && make -j`nproc`
   ```

Next follow the guide in each sub folder to run each example.

## Examples list

- [**`custom_function`**](./custom_function/README.md): Configures FWE with the custom function
  examples provided in the [custom function guide](../docs/dev-guide/custom-function-dev-guide.md).
- [**`mqtt_pub_sub`**](./mqtt_pub_sub/README.md): Configures an extra MQTT subscription to the
  `ping` topic, and responds by publishing on the `pong` topic.
- [**`network_agnostic_actuator_commands`**](./network_agnostic_actuator_commands/README.md):
  Configures an extra actuator network interface that logs the `setActuatorValue` request and
  responds with `SUCCEEDED`. See
  [`AcCommandDispatcher`](./network_agnostic_actuator_commands/AcCommandDispatcher.h) and the
  [Network agnostic actuator commands guide](../docs/dev-guide/network-agnostic-dev-guide.md).
- [**`network_agnostic_data_collection`**](./network_agnostic_data_collection/README.md): Configures
  three [Network agnostic data collection (NADC)](../docs/dev-guide/network-agnostic-dev-guide.md)
  data sources:
  1. One using the built-in
     [`NamedSignalDataSource`](../include/aws/iotfleetwise/NamedSignalDataSource.h).
  1. One using the [`NamedSignalDataSource`](../include/aws/iotfleetwise/NamedSignalDataSource.h)
     but requiring extra configuration parameters, see
     [`MyCounterDataSource`](./network_agnostic_data_collection/MyCounterDataSource.h).
  1. One fully-custom data source using the NADC decoder string as CSV data, see
     [`MyCustomDataSource`](./network_agnostic_data_collection/MyCustomDataSource.h).
- [**`s3_upload`**](./s3_upload/README.md): Uploads a configured file to the configured S3 bucket
  using the
  [IoT Credentials Provider](https://docs.aws.amazon.com/iot/latest/developerguide/authorizing-direct-aws.html).
- [**`someip`**](./someip/README.md): Illustrates how to use custom FIDL and FDEPL files with FWE
  for [Network agnostic data collection (NADC)](../docs/dev-guide/network-agnostic-dev-guide.md).

## FWE boostrap and configuration hooks

The [`IoTFleetWiseEngine`](../include/aws/iotfleetwise/IoTFleetWiseEngine.h) module is the main
'bootstrap' module of FWE that creates objects and starts threads according to the static
configuration JSON file. External projects using FWE as a C++ dependency can also use this module to
bootstrap the FWE components. `IoTFleetWiseEngine` provides configuration hooks that allow for the
following integration with external code:

- **`IoTFleetWiseEngine::setConnectivityModuleConfigHook`**: use this hook for configuring an
  external connectivity module. This is useful when the system that FWE runs on does not have direct
  internet access, but rather performs all communication with the cloud via a custom tunnel. The
  hook should set `IoTFleetWiseEngine::mConnectivityModule` to the created object that meets the
  [`IConnectivityModule`](../include/aws/iotfleetwise/IConnectivityModule.h) interface.

- **`IoTFleetWiseEngine::setNetworkInterfaceConfigHook`**: use this hook for configuring extra
  network interfaces for use by FWE. The network interfaces can be used as data sources or command
  actuators.

- **`IoTFleetWiseEngine::setStartupConfigHook`**: use this hook for configuring named-signal data
  sources using the built-in
  [`NamedSignalDataSource`](../include/aws/iotfleetwise/NamedSignalDataSource.h), custom functions
  and extra MQTT subscriptions.

- **`IoTFleetWiseEngine::setShutdownHook`**: use this hook for tearing-down the components created
  in the other hooks during shutdown.

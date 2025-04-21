# Demo of AWS IoT FleetWise for Device Shadow over SOME/IP

This guide demonstrates AWS IoT FleetWise (FWE) which supports Device Shadow over SOME/IP service.

## Overview of Device Shadow over SOME/IP service

[Franca IDL](https://github.com/franca/franca) is the industry standard schema format for specifying
SOME/IP messages, and [CommonAPI](https://covesa.github.io/capicxx-core-tools/) is the industry
standard serialization format for SOME/IP. Franca IDL files are called 'FIDL' files, which are
transport-layer-independent, and their 'deployment' on SOME/IP is specified in 'FDEPL' files that
specify which SOME/IP service transports each message. The CommonAPI library provides a code
generator that takes the FIDL and FDEPL files and generates C++ code to implement the Franca
interfaces for both the client and server.

FWE support access on
[AWS IoT Device Shadow service](https://docs.aws.amazon.com/iot/latest/developerguide/iot-device-shadows.html)
via SOME/IP. It supports methods for deleting, getting and updating device shadows. When the
application calls these methods, FWE prepare and publish appropriate request messages on
correspondent topics to the server. It then gather responses from the server and provide result to
the application. FWE also supports broadcasting an event when a device shadow changes (from the
server) to registered applications. For more details on supported methods and event, please see
[DeviceShadowOverSomeipInterface.fidl](../../interfaces/someip/fidl/DeviceShadowOverSomeipInterface.fidl)
and
[DeviceShadowOverSomeipInterface.fdepl](../../interfaces/someip/fidl/DeviceShadowOverSomeipInterface.fdepl).

FWE is compiled to support the Device Shadow over SOME/IP interface. A Device Shadow over SOME/IP
simulator called `someip_device_shadow_editor` is provided in order to simulate another node in the
system which plays the role of an example application that calls/handles aforementioned
methods/event. `someip_device_shadow_editor` is also compiled with support for Device Shadow over
SOME/IP interface.

In this demo, firstly FWE is provisioned and configured to run with Device Shadow over SOME/IP
service. Secondly `someip_device_shadow_editor` is used to show how the application accesses AWS IoT
Device Shadow service via SOME/IP.

## Prerequisites

- Access to an AWS Account with administrator privileges.
- Logged in to the AWS Console in the `us-east-1` region using the account with administrator
  privileges.
  - Note: if you would like to use a different region you will need to change `us-east-1` to your
    desired region in each place that it is mentioned below.
  - Note: AWS IoT FleetWise is currently available in
    [these](https://docs.aws.amazon.com/general/latest/gr/iotfleetwise.html) regions.
- A local Windows, Linux or MacOS machine.

## Launch your development machine

An Ubuntu 22.04 development machine with 200GB free disk space will be required. A local Intel
x86_64 (amd64) machine can be used, however it is recommended to use the following instructions to
launch an AWS EC2 Graviton (arm64) instance. Pricing for EC2 can be found,
[here](https://aws.amazon.com/ec2/pricing/on-demand/).

1. Launch an EC2 Graviton instance with administrator permissions:
   [**Launch CloudFormation Template**](https://us-east-1.console.aws.amazon.com/cloudformation/home?region=us-east-1#/stacks/quickcreate?templateUrl=https%3A%2F%2Faws-iot-fleetwise.s3.us-west-2.amazonaws.com%2Flatest%2Fcfn-templates%2Ffwdev.yml&stackName=fwdev).
2. Enter the **Name** of an existing SSH key pair in your account from
   [here](https://us-east-1.console.aws.amazon.com/ec2/v2/home?region=us-east-1#KeyPairs:).
   - Do not include the file suffix `.pem`.
   - If you do not have an SSH key pair, you will need to create one and download the corresponding
     `.pem` file. Be sure to update the file permissions: `chmod 400 <PATH_TO_PEM>`
3. **Select the checkbox** next to _'I acknowledge that AWS CloudFormation might create IAM
   resources with custom names.'_
4. Choose **Create stack**.
5. Wait until the status of the Stack is **CREATE_COMPLETE**; this can take up to five minutes.
6. Select the **Outputs** tab, copy the EC2 IP address, and connect via SSH from your local machine
   to the development machine.

   ```bash
   ssh -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>
   ```

## Obtain the FWE code

1. Run the following _on the development machine_ to clone the latest FWE source code from GitHub.

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge
   ```

## Download or build the FWE binary

**To quickly run the demo**, download the pre-built FWE binary:

- If your development machine is ARM64 (the default if you launched an EC2 instance using the
  CloudFormation template above):

  ```bash
  cd ~/aws-iot-fleetwise-edge \
  && mkdir -p build \
  && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
      https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-arm64.tar.gz  \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz -C build aws-iot-fleetwise-edge \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz tools/someip_device_shadow_editor/someip_device_shadow_editor.so
  ```

- If your development machine is x86_64:

  ```bash
  cd ~/aws-iot-fleetwise-edge \
  && mkdir -p build \
  && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
      https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-amd64.tar.gz  \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz -C build aws-iot-fleetwise-edge \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz tools/someip_device_shadow_editor/someip_device_shadow_editor.so
  ```

**Alternatively if you would like to build the FWE binary from source,** follow these instructions.
If you already downloaded the binary above, skip to the next section.

1. Install the dependencies for FWE with SOME/IP support:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && sudo -H ./tools/install-deps-native.sh --with-someip-support \
   && sudo ldconfig
   ```

1. Compile FWE with SOME/IP support and the SOME/IP simulator:

   ```bash
   ./tools/build-fwe-native.sh --with-someip-support
   ```

## Provision and run FWE

1. Open a new terminal _on the development machine_, and run the following to provision credentials
   for the vehicle and configure the vehicle accordingly:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && mkdir -p build_config \
   && ./tools/provision.sh \
       --region us-east-1 \
       --vehicle-name fwdemo-device-shadow-over-someip \
       --certificate-pem-outfile build_config/certificate.pem \
       --private-key-outfile build_config/private-key.key \
       --endpoint-url-outfile build_config/endpoint.txt \
       --vehicle-name-outfile build_config/vehicle-name.txt \
   && ./tools/configure-fwe.sh \
       --input-config-file configuration/static-config.json \
       --output-config-file build_config/config-0.json \
       --log-color Yes \
       --log-level Trace \
       --vehicle-name `cat build_config/vehicle-name.txt` \
       --endpoint-url `cat build_config/endpoint.txt` \
       --certificate-file `realpath build_config/certificate.pem` \
       --private-key-file `realpath build_config/private-key.key` \
       --persistency-path `realpath build_config` \
   && OUTPUT_CONFIG=`jq -r '.staticConfig.deviceShadowOverSomeip={"someipApplicationName":"deviceShadowOverSomeipInterface"}' build_config/config-0.json` \
   && echo "${OUTPUT_CONFIG}" > build_config/config-0.json
   ```

2. Run FWE:

   ```bash
   ./build/aws-iot-fleetwise-edge build_config/config-0.json
   ```

   You should see the following messages in the log indicating that FWE has successfully launched
   Device Shadow over SOME/IP service:

   ```
   [info] io thread id from application: 0100 (someipDeviceShadowService) is: 7f7f7974d700 TID: 2832
   [info] io thread id from application: 0100 (someipDeviceShadowService) is: 7f7f72ffd700 TID: 2836
   [info] vSomeIP 3.4.10 | (default)
   ```

## Programmatically call Device Shadow over SOME/IP methods

A simulator is used to model another node in the vehicle network that calls/handles Device Shadow
over SOME/IP methods/event.

1. Start the Device Shadow over SOME/IP simulator:

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/someip_device_shadow_editor \
   && python3 someip_device_shadow_editor_sim.py
   ```

## Interactively call Device Shadow over SOME/IP methods

Instead of running Device Shadow over SOME/IP simulator with pre-defined sequence of methods, you
can try calling any method manually.

1. Open a new terminal _on the development machine_ and run the alternate script:

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/someip_device_shadow_editor \
   && python3 someip_device_shadow_editor_repl.py
   ```

2. Wait until it successfully subscribes to Device Shadow over SOME/IP service.

   ```
   [info] ON_AVAILABLE(0101): [1235.5679:1.0]
   [info] SUBSCRIBE ACK(0100): [1235.5679.80f2.80f2]
   ```

3. Call any method manually.

   - Type `help` to get familiarity with command usage.
   - To get shadow, type `get <SHADOW_NAME>`, where `<SHADOW_NAME>` can be blank to get the
     'classic' shadow, **but the space after `get` is still required**.

   ```
   get
   get shadow-x
   ```

   - To update shadow, type `update <SHADOW_NAME> <SHADOW_DOCUMENT>`, where `<SHADOW_NAME>` can be
     blank to update the 'classic' shadow, **but the space after `update` is still required**.

   ```
   update  {"state":{"desired":{"temperature":25},"reported":{"temperature":22}}}
   update shadow-x {"state":{"desired":{"type":"named shadow"},"reported":{"type":"named shadow x"}}}
   ```

   - To delete shadow, type `delete <SHADOW_NAME>`, where `<SHADOW_NAME>` can be blank to delete the
     'classic' shadow, **but the space after `delete` is still required**.

   ```
   delete
   delete shadow-x
   ```

   - Type `exit` or `quit` to stop.

## Clean up

1. Run the following _on the development machine_ to clean up resources created by the
   `provision.sh`.

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/ \
   && ./provision.sh \
      --vehicle-name fwdemo-device-shadow-over-someip \
      --region us-east-1 \
      --only-clean-up
   ```

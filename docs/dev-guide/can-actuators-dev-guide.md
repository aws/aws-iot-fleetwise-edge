# CAN actuators demo

<!-- prettier-ignore -->
> [!NOTE]
> This guide makes use of "gated" features of AWS IoT FleetWise for which you will need to request
> access. See
> [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html) for
> more information, or contact the
> [AWS Support Center](https://console.aws.amazon.com/support/home#/).

This guide demonstrates how to use AWS IoT FleetWise to implement a CAN command dispatcher for use
with the remote commands feature, along with the
[Network agnostic actuator commands (NADC)](./network-agnostic-dev-guide.md) feature. The NADC
feature allows the actuators to be identified by the full-qualified-name (FQN) at the edge. The demo
uses application-specific request and response CAN payload formats to forward the command request to
CAN, including the 'command ID', 'issued timestamp' and 'execution timeout' parameters as well as
the requested actuator value. A Python script called `can_command_server.py` is used to simulate
another vehicle in the network that receives the request and responds on CAN with a response
message. The Reference Implementation for AWS IoT FleetWise (FWE) receives this response, and sends
it back to the cloud.

The format of the CAN request and response messages implemented in
[`CanCommandDispatcher`](../../include/aws/iotfleetwise/CanCommandDispatcher.h) is as follows:

- The command CAN request payload is formed from the null-terminated command ID string, a `uint64_t`
  issued timestamp in ms since epoch, a `uint64_t` relative execution timeout in ms since the issued
  timestamp, and one actuator argument serialized in network byte order. A relative timeout value of
  zero means no timeout. Example with command ID `"01J3N9DAVV10AA83PZJX561HPS"`, issued timestamp of
  `1723134069000`, relative timeout of `1000`, and actuator datatype `int32_t` with value
  `1234567890`:

```
|----------------------------------------|----------------------------------------|---------------------------------|---------------------------------|---------------------------|
| Payload byte:                          | 0    | 1    | ... | 24   | 25   | 26   | 27   | 28   | ... | 33   | 34   | 35   | 36   | ... | 41   | 42   | 43   | 44   | 45   | 46   |
|----------------------------------------|----------------------------------------|---------------------------------|---------------------------------|---------------------------|
| Value:                                 | 0x30 | 0x31 | ... | 0x50 | 0x53 | 0x00 | 0x00 | 0x00 | ... | 0x49 | 0x08 | 0x00 | 0x00 | ... | 0x03 | 0xE8 | 0x49 | 0x96 | 0x02 | 0xD2 |
|----------------------------------------|----------------------------------------|---------------------------------|---------------------------------|---------------------------|
Command ID (null terminated string)-------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Issued timestamp (uint64_t network byte order)-------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Execution timeout (uint64_t network byte order)----------------------------------------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Request argument (int32_t network byte order)----------------------------------------------------------------------------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

- The command CAN response payload is formed from the null-terminated command ID string, a 1-byte
  command status code, a 4-byte `uint32_t` reason code, and a null-terminated reason description
  string. The values of the status code correspond with the enum `CommandStatus` Example with
  command ID `"01J3N9DAVV10AA83PZJX561HPS"`, response status `CommandStatus::EXECUTION_FAILED`,
  reason code `0x0001ABCD`, and reason description `"hello"`:

```
|----------------------------------------|----------------------------------------|------|---------------------------|-----------------------------------------|
| Payload byte:                          | 0    | 1    | ... | 24   | 25   | 26   | 27   | 28   | 29   | 30   | 31   | 32   | 33   | 34   | 35   | 36   | 37   |
|----------------------------------------|----------------------------------------|------|---------------------------|-----------------------------------------|
| Value:                                 | 0x30 | 0x31 | ... | 0x50 | 0x53 | 0x00 | 0x03 | 0x00 | 0x01 | 0xAB | 0xCD | 0x68 | 0x65 | 0x6C | 0x6C | 0x6F | 0x00 |
|----------------------------------------|----------------------------------------|------|---------------------------|-----------------------------------------|
Command ID (null terminated string)-------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Command status (enum CommandStatus)------------------------------------------------^^^^^^
Reason code (uint32_t network byte order)-------------------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^
Reason description (null terminated string)---------------------------------------------------------------------------^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

The table `EXAMPLE_CAN_INTERFACE_SUPPORTED_ACTUATOR_MAP` in
[`IoTFleetWiseEngine`](../../src/IoTFleetWiseEngine.cpp) defines the mapping between the FQN of the
actuator signal and the CAN request and response message IDs, along with the expected datatype of
the signal. (Note: when the most-significant bit of the CAN message ID is set, it denotes a 29-bit
extended CAN ID.):

```cpp
static const std::unordered_map<std::string, CanCommandDispatcher::CommandConfig>
    EXAMPLE_CAN_INTERFACE_SUPPORTED_ACTUATOR_MAP = {
        { "Vehicle.actuator6", { 0x00000123, 0x00000456, SignalType::INT32  } },
        { "Vehicle.actuator7", { 0x80000789, 0x80000ABC, SignalType::DOUBLE } },
};
```

These IDs and types can also be found in the configuration table at the top of
[`can_command_server.py`](../../tools/cansim/can_command_server.py)

## Prerequisites

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
- A local Windows, Linux or MacOS machine.

## Launch your development machine

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

## Download or build the FWE binary

**To quickly run the demo**, download the pre-built FWE binary, and install CAN support:

- If your development machine is ARM64 (the default if you launched an EC2 instance using the
  CloudFormation template above):

  ```bash
  cd ~/aws-iot-fleetwise-edge \
  && mkdir -p build \
  && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
      https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-arm64.tar.gz  \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz -C build aws-iot-fleetwise-edge \
  && sudo -H ./tools/install-socketcan.sh
  ```

- If your development machine is x86_64:

  ```bash
  cd ~/aws-iot-fleetwise-edge \
  && mkdir -p build \
  && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
      https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-amd64.tar.gz  \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz -C build aws-iot-fleetwise-edge \
  && sudo -H ./tools/install-socketcan.sh
  ```

**Alternatively if you would like to build the FWE binary from source,** follow these instructions.
If you already downloaded the binary above, skip to the next section.

1. Install the dependencies for FWE and the CAN simulator:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && sudo -H ./tools/install-deps-native.sh \
   && sudo -H ./tools/install-socketcan.sh \
   && sudo ldconfig
   ```

1. Compile FWE with remote commands support:

   ```bash
   ./tools/build-fwe-native.sh --with-remote-commands-support
   ```

## Start the CAN command server

A simulator is used to model another node in the vehicle network that responds to CAN command
requests.

1. Start the CAN command server:

   ```bash
   cd tools/cansim \
   && python3 can_command_server.py --interface vcan0
   ```

## Provision and run FWE

1. Open a new terminal _on the development machine_, and run the following to provision credentials
   for the vehicle and configure the network interface for CAN commands:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && mkdir -p build_config \
   && ./tools/provision.sh \
       --region us-east-1 \
       --vehicle-name fwdemo-can-actuators \
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
       --session-expiry-interval-seconds 3600 \
       --can-command-interface vcan0
   ```

1. Run FWE:

   ```bash
   ./build/aws-iot-fleetwise-edge build_config/config-0.json
   ```

## Run the AWS IoT FleetWise demo script

The instructions below will register your AWS account for AWS IoT FleetWise, create a demonstration
vehicle model, register the virtual vehicle created in the previous section.

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
       --vehicle-name fwdemo-can-actuators \
       --node-file custom-nodes-can-actuators.json \
       --decoder-file custom-decoders-can-actuators.json \
       --network-interface-file network-interface-custom-can-actuators.json
   ```

   The demo script:

   1. Registers your AWS account with AWS IoT FleetWise, if not already registered.
   1. Creates a signal catalog, containing `custom-nodes-can-actuators.json` which includes the CAN
      actuator signals.
   1. Creates a model manifest that references the signal catalog with all of the signals.
   1. Activates the model manifest.
   1. Creates a decoder manifest linked to the model manifest using
      `custom-decoders-can-actuators.json` for decoding the CAN signals from the network interface
      `network-interface-custom-can-actuators.json`.
   1. Updates the decoder manifest to set the status as `ACTIVE`.
   1. Creates a vehicle with a name equal to `fwdemo-can-actuators`, the same as the name passed to
      `provision.sh`.
   1. Creates a fleet.
   1. Associates the vehicle with the fleet.

### Remote Command Execution

The following steps will send a CAN command via the AWS IoT FleetWise 'remote commands' feature.

1. Run the following command _on the development machine_ to create an IAM role to generate the
   command payload:

   ```bash
   SERVICE_ROLE_ARN=`./manage-service-role.sh \
      --service-role IoTCreateCommandPayloadServiceRole \
      --service-principal iot.amazonaws.com \
      --actions iotfleetwise:GenerateCommandPayload \
      --resources '*'`
   ```

1. Next create a remote command to send the CAN command with message ID `0x123` and expect a
   response on CAN message ID `0x456`. This CAN command is mapped via the decoder manifest to the
   'actuator' node `Vehicle.actuator6` in the signal catalog.

   ```bash
   aws iot create-command --command-id actuator6-command --namespace "AWS-IoT-FleetWise" \
      --region us-east-1 \
      --role-arn ${SERVICE_ROLE_ARN} \
      --mandatory-parameters '[{
         "name": "$actuatorPath.Vehicle.actuator6",
         "defaultValue": { "S": "0" }
      }]'
   ```

1. Run the following command to start the execution of the command defined above with the value to
   set for the actuator.

   ```bash
   JOBS_ENDPOINT_URL=`aws iot describe-endpoint --region us-east-1 --endpoint-type iot:Jobs | jq -j .endpointAddress` \
   && ACCOUNT_ID=`aws sts get-caller-identity | jq -r .Account` \
   && COMMAND_EXECUTION_ID=`aws iot-jobs-data start-command-execution \
      --region us-east-1 \
      --command-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:command/actuator6-command \
      --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
      --parameters '{
        "$actuatorPath.Vehicle.actuator6":
            { "S": "10" }
      }' \
      --endpoint-url https://${JOBS_ENDPOINT_URL} | jq -r .executionId` \
   && echo "Command execution id: ${COMMAND_EXECUTION_ID}"
   ```

1. Run the following command to get the command execution status.

   ```bash
   aws iot get-command-execution \
      --region us-east-1 \
      --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
      --execution-id ${COMMAND_EXECUTION_ID}
   ```

1. You should see the following output indicating the command was successfully executed. Note that
   the `reasonCode` (uint32) and `reasonDescription` (string) are extensible result information
   fields. Refer to [ICommandDispatcher.h](../../include/aws/iotfleetwise/ICommandDispatcher.h) for
   the reason codes defined by FWE. The OEM range of reason codes begins at 65536. In this example
   implementation the `reasonCode` is set to `0x1234` (4660), and `reasonDescription` is set to
   `"hello"` by the CAN command
   server[`can_command_server.py`](../../tools/cansim/can_command_server.py).

   ```json
   {
     "executionId": "<COMMAND_EXECUTION_ID>",
     "commandArn": "arn:aws:iot:us-east-1:<ACCOUNT_ID>:command/actuator6-command",
     "targetArn": "arn:aws:iot:us-east-1:<ACCOUNT_ID>:thing/fwdemo-can-actuators",
     "status": "SUCCEEDED",
     "statusReason": {
       "reasonCode": "4660",
       "reasonDescription": "hello"
     },
     "parameters": {
       "$actuatorPath.Vehicle.actuator6": {
         "S": "10"
       }
     },
     "executionTimeoutSeconds": 10,
     "createdAt": "<CREATION_TIME>",
     "lastUpdatedAt": "<LAST_UPDATE_TIME>",
     "completedAt": "<COMPLETED_TIME>"
   }
   ```

   In the FWE log you should see the following indicating that the command was successfully
   executed:

   ```
   [TRACE] [ActuatorCommandManager.cpp:125] [processCommandRequest()]: [Processing Command Request with ID: <COMMAND_EXECUTION_ID>]
   [INFO ] [CanCommandDispatcher.cpp:365] [setActuatorValue()]: [Sending request for actuator Vehicle.actuator6 and command id <COMMAND_EXECUTION_ID>]
   [INFO ] [CanCommandDispatcher.cpp:390] [operator()()]: [Request sent for actuator Vehicle.actuator6 and command id <COMMAND_EXECUTION_ID>]
   [INFO ] [CanCommandDispatcher.cpp:209] [handleCanFrameReception()]: [Received response for actuator Vehicle.actuator6 with command id <COMMAND_EXECUTION_ID>, status SUCCEEDED, reason code 4660, reason description hello]
   ```

### Long-running commands

It is possible for commands to take an extended time to complete. In this case the vehicle can
report the command status as `IN_PROGRESS` to indicate that the command has been received and is
being run, before the final status of `SUCCEEDED` etc. is reported.

In the example CAN commands provided, `Vehicle.actuator7` is configured as such a "long-running
command". After running of this command is started the CAN command server will periodically notify
FWE that the status is `CommandStatus::IN_PROGRESS`. The intermediate status is sent to the cloud
and can also be obtained by calling the `aws iot get-command-execution` API.

1. Run the following to create the long-running command:

   ```bash
   aws iot create-command --command-id actuator7-command --namespace "AWS-IoT-FleetWise" \
      --region us-east-1 \
      --role-arn ${SERVICE_ROLE_ARN} \
      --mandatory-parameters '[{
         "name": "$actuatorPath.Vehicle.actuator7",
         "defaultValue": { "S": "0" }
      }]'
   ```

1. Then start the command:

   ```bash
      COMMAND_EXECUTION_ID=`aws iot-jobs-data start-command-execution \
         --region us-east-1 \
         --command-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:command/actuator7-command \
         --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
         --parameters '{
         "$actuatorPath.Vehicle.actuator7":
               { "S": "10" }
         }' \
         --endpoint-url https://${JOBS_ENDPOINT_URL} \
         --execution-timeout 20 | jq -r .executionId` \
      && echo "Command execution id: ${COMMAND_EXECUTION_ID}"
   ```

1. Now repeatedly run this command to get the command status:

   ```bash
   aws iot get-command-execution \
      --region us-east-1 \
      --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
      --execution-id ${COMMAND_EXECUTION_ID}
   ```

   The command takes 10 seconds to complete. In this time you will see that the status is
   `IN_PROGRESS`. After the command completes the status changes to `SUCCEEDED`.

### Concurrent commands

It is possible for commands to be executed concurrently, even for the same actuator. Each execution
is uniquely identified by the execution ID. In the following example, 3 executions of the
`Vehicle.actuator7` command are started spaced by 1 second. Since each execution takes 10 seconds to
complete, all 3 will run in parallel.

1. Run the following to begin 3 executions of the `Vehicle.actuator7` command:

   ```bash
   COMMAND_EXECUTION_IDS=() \
   && for ((i=0; i<3; i++)); do
      if ((i>0)); then sleep 1; fi
      COMMAND_EXECUTION_ID=`aws iot-jobs-data start-command-execution \
         --region us-east-1 \
         --command-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:command/actuator7-command \
         --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
         --parameters '{
         "$actuatorPath.Vehicle.actuator7":
               { "S": "10" }
         }' \
         --endpoint-url https://${JOBS_ENDPOINT_URL} \
         --execution-timeout 20 | jq -r .executionId`
      echo "Command execution ${i} id: ${COMMAND_EXECUTION_ID}"
      COMMAND_EXECUTION_IDS+=("${COMMAND_EXECUTION_ID}")
   done
   ```

1. Now repeatedly run the following to get the status of the 3 commands as they run in parallel:

   ```bash
   for ((i=0; i<3; i++)); do
      echo "---------------------------"
      echo "Command execution ${i} status:"
      aws iot get-command-execution \
         --region us-east-1 \
         --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
         --execution-id ${COMMAND_EXECUTION_IDS[i]}
   done
   ```

### Offline commands

It is possible for a command execution to be started while FWE is offline, then FWE will begin
execution of the command when it comes online so long as the following prerequisites are met:

- FWE has successfully connected via MQTT at least once, with persistent session enabled and the
  MQTT session timeout has not elapsed. To enable persistent session, set
  `.staticConfig.mqttConnection.sessionExpiryIntervalSeconds` in the config file or
  `--session-expiry-interval-seconds` when running `configure-fwe.sh` to a non-zero value
  sufficiently large.
- Persistency is enabled for FWE (so that the decoder manifest is available immediately when FWE
  starts).
- The command timeout has not been exceeded.
- The responding CAN actuator is available when FWE is started (in this case the
  `can_command_server.py` tool).

The following steps demonstrate offline commands:

1. Switch to the terminal running FWE, and stop it using `CTRL-C`.

1. Switch to the terminal used to run the AWS CLI commands, and start execution of a command with a
   30 second timeout:

   ```bash
   COMMAND_EXECUTION_ID=`aws iot-jobs-data start-command-execution \
      --region us-east-1 \
      --command-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:command/actuator6-command \
      --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
      --parameters '{
        "$actuatorPath.Vehicle.actuator6":
            { "S": "456" }
      }' \
      --endpoint-url https://${JOBS_ENDPOINT_URL} \
      --execution-timeout 30 | jq -r .executionId` \
   && echo "Command execution id: ${COMMAND_EXECUTION_ID}"
   ```

1. Get the current status of the command, which will remain as `CREATED` since FWE is not running:

   ```bash
   aws iot get-command-execution \
      --region us-east-1 \
      --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
      --execution-id ${COMMAND_EXECUTION_ID}
   ```

1. Switch to the FWE terminal, and restart it by running:

   ```bash
   ./build/aws-iot-fleetwise-edge build_config/config-0.json
   ```

1. Switch to the AWS CLI terminal, and run the following to get the new status of the command, which
   should be `SUCCEEDED`. Since FWE rejoined an existing MQTT session and the command was published
   with QoS 1 (at least once), the MQTT broker sends the command to FWE as soon as it connects to
   the cloud. FWE is able to execute the command, since it has not timed out, the decoder manifest
   is available (as persistency for FWE is enabled), and the CAN command server is available.

   ```bash
   aws iot get-command-execution \
      --region us-east-1 \
      --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwdemo-can-actuators \
      --execution-id ${COMMAND_EXECUTION_ID}
   ```

1. Repeat the above, but this time wait longer than 30s before restarting FWE. In this case FWE will
   still receive the command request from cloud, but since the timeout has expired it will not be
   executed and the returned status will be `TIMED_OUT`.

## Clean up

1. Run the following _on the development machine_ to clean up resources created by the
   `provision.sh` and `demo.sh` scripts.

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
   && ./clean-up.sh \
   && ../provision.sh \
      --vehicle-name fwdemo-can-actuators \
      --region us-east-1 \
      --only-clean-up \
   && ./manage-service-role.sh \
      --service-role IoTCreateCommandPayloadServiceRole \
      --clean-up
   ```

1. Delete the CloudFormation stack for your development machine, which by default is called `fwdev`:
   https://us-east-1.console.aws.amazon.com/cloudformation/home

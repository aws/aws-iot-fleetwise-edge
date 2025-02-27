# Network agnostic actuator commands example

<!-- prettier-ignore -->
> [!NOTE]
> This is a "gated" feature of AWS IoT FleetWise for which you will need to request access. See
> [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html)
> for more information, or contact the
> [AWS Support Center](https://console.aws.amazon.com/support/home#/).

This example configures a network interface for actuator commands that logs the `setActuatorValue`
request and responds with `SUCCEEDED`. Also refer to the
[Network agnostic actuator commands guide](../../docs/dev-guide/network-agnostic-dev-guide.md#remote-commands)
and the
[cloud developer guide](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/network-agnostic-data-collection.html).

This guide assumes you have already configured your development machine and built the examples using
[these](../README.md#building) instructions.

## Provisioning and Configuration

Run the following to provision credentials for the vehicle:

```bash
cd ~/aws-iot-fleetwise-edge/examples/network_agnostic_actuator_commands \
&& mkdir -p build_config \
&& ../../tools/provision.sh \
    --region us-east-1 \
    --vehicle-name fwe-example-network-agnostic-actuator-commands \
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
&& AC_COMMAND_INTERFACE=`echo {} | jq ".interfaceId=\"AC_ACTUATORS\"" \
    | jq ".type=\"acCommandInterface\""` \
&& OUTPUT_CONFIG=`jq ".networkInterfaces+=[${AC_COMMAND_INTERFACE}]" build_config/config-0.json` \
&& echo "${OUTPUT_CONFIG}" > build_config/config-0.json
```

## Running the example

1. Run the example with the config file:

   ```bash
   ../build/network_agnostic_actuator_commands/fwe-example-network-agnostic-actuator-commands \
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
       --vehicle-name fwe-example-network-agnostic-actuator-commands \
       --node-file ../../examples/network_agnostic_actuator_commands/custom-nodes-ac-actuators.json \
       --decoder-file ../../examples/network_agnostic_actuator_commands/custom-decoders-ac-actuators.json \
       --network-interface-file ../../examples/network_agnostic_actuator_commands/network-interface-custom-ac-actuators.json
   ```

1. Run the following command _on the development machine_ to create an IAM role to generate the
   command payload:

   ```bash
   SERVICE_ROLE_ARN=`./manage-service-role.sh \
      --service-role IoTCreateCommandPayloadServiceRole \
      --service-principal iot.amazonaws.com \
      --actions iotfleetwise:GenerateCommandPayload \
      --resources '*'`
   ```

1. Next create a remote command:

   ```bash
   aws iot create-command --command-id activateAC --namespace "AWS-IoT-FleetWise" \
      --region us-east-1 \
      --role-arn ${SERVICE_ROLE_ARN} \
      --mandatory-parameters '[{
         "name": "$actuatorPath.Vehicle.ActivateAC",
         "defaultValue": { "B": false }
      }]'
   ```

1. Run the following command to start the execution of the command defined above with the value to
   set for the actuator.

   ```bash
   JOBS_ENDPOINT_URL=`aws iot describe-endpoint --region us-east-1 --endpoint-type iot:Jobs | jq -j .endpointAddress` \
   && ACCOUNT_ID=`aws sts get-caller-identity | jq -r .Account` \
   && COMMAND_EXECUTION_ID=`aws iot-jobs-data start-command-execution \
      --region us-east-1 \
      --command-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:command/activateAC \
      --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwe-example-network-agnostic-actuator-commands \
      --parameters '{
        "$actuatorPath.Vehicle.ActivateAC":
            { "B": true }
      }' \
      --endpoint-url https://${JOBS_ENDPOINT_URL} | jq -r .executionId` \
   && echo "Command execution id: ${COMMAND_EXECUTION_ID}"
   ```

1. Run the following command to get the command execution status.

   ```bash
   aws iot get-command-execution \
      --region us-east-1 \
      --target-arn arn:aws:iot:us-east-1:${ACCOUNT_ID}:thing/fwe-example-network-agnostic-actuator-commands \
      --execution-id ${COMMAND_EXECUTION_ID}
   ```

1. You should see the following output indicating the command was successfully executed. Note that
   the `reasonCode` (uint32) and `reasonDescription` (string) are extensible result information
   fields. Refer to [ICommandDispatcher.h](../../include/aws/iotfleetwise/ICommandDispatcher.h) for
   the reason codes defined by FWE. The OEM range of reason codes begins at 65536. In this example
   implementation the `reasonCode` is set to `0x1234` (4660), and `reasonDescription` is set to
   `"Success"` by [`AcCommandDispatcher`](./AcCommandDispatcher.cpp)

   ```json
   {
     "executionId": "<COMMAND_EXECUTION_ID>",
     "commandArn": "arn:aws:iot:us-east-1:<ACCOUNT_ID>:command/activateAC",
     "targetArn": "arn:aws:iot:us-east-1:<ACCOUNT_ID>:thing/fwe-example-network-agnostic-actuator-commands
     "status": "SUCCEEDED",
     "statusReason": {
       "reasonCode": "4660",
       "reasonDescription": "Success"
     },
     "parameters": {
       "$actuatorPath.Vehicle.ActivateAC": {
         "B": true
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
   [TRACE] [ActuatorCommandManager.cpp:124] [processCommandRequest()]: [Processing Command Request with ID: <COMMAND_EXECUTION_ID>]
   [INFO ] [AcCommandDispatcher.cpp:28] [setActuatorValue()]: [Actuator Vehicle.ActivateAC executed successfully for command ID <COMMAND_EXECUTION_ID>]
   ```

# FWE with AWS IoT Greengrass V2 IPC

The Reference Implementation for AWS IoT FleetWise ("FWE") provides an integration with the AWS IoT
Greengrass V2 IPC client. It enables FWE to be deployed as a Greengrass V2 Component and allows it
to connect to AWS IoT Core via the Greengrass IPC mechanism. Authentication with IoT Core is handled
by Greengrass, simplifying the configuration of IoT FleetWise.

## Prerequisites

- Access to an AWS Account with administrator privileges.
- Logged in to the AWS Console in the `us-east-1` region using the account with administrator
  privileges.
  - Note: if you would like to use a different region you will need to change `us-east-1` to your
    desired region in each place that it is mentioned below.
  - Note: AWS IoT FleetWise is currently available in
    [these](https://docs.aws.amazon.com/general/latest/gr/iotfleetwise.html) regions.
- A local Linux or MacOS machine.

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

**To quickly run the demo**, download the pre-built FWE binary:

- If your development machine is ARM64 (the default if you launched an EC2 instance using the
  CloudFormation template above):

  ```bash
  cd ~/aws-iot-fleetwise-edge \
  && mkdir -p build \
  && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
      https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-arm64.tar.gz  \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz -C build aws-iot-fleetwise-edge
  ```

- If your development machine is x86_64:

  ```bash
  cd ~/aws-iot-fleetwise-edge \
  && mkdir -p build \
  && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
      https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-amd64.tar.gz  \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz -C build aws-iot-fleetwise-edge
  ```

**Alternatively if you would like to build the FWE binary from source,** follow these instructions.
If you already downloaded the binary above, skip to the next section.

1. Install the dependencies for FWE :

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && sudo -H ./tools/install-deps-native.sh --with-greengrassv2-support \
   && sudo -H ./tools/install-socketcan.sh \
   && sudo -H ./tools/install-cansim.sh
   ```

1. Compile FWE with Greengrass V2 support:

   ```bash
   ./tools/build-fwe-native.sh --with-greengrassv2-support
   ```

## Install IoT Greengrass core in local machine

1. Install java:

   ```bash
   sudo apt-get install default-jre
   ```

1. Install IoT Greengrass V2 core on the development machine, by following these instructions:
   https://docs.aws.amazon.com/greengrass/v2/developerguide/install-greengrass-v2-cli.html

## Configure the FWE as the deployment of Greengrass V2

Run the following command to create the configuration file for IoT FleetWise. Replace `fwdemo` with
the thing name that you used above to register the IoT Greengrass device:

```bash
cd ~/aws-iot-fleetwise-edge \
&& mkdir -p tools/greengrassV2/artifacts/com.amazon.aws.IoTFleetWise/1.0.0 \
&& ./tools/configure-fwe.sh \
    --input-config-file ./configuration/static-config.json \
    --output-config-file tools/greengrassV2/artifacts/com.amazon.aws.IoTFleetWise/1.0.0/config-0.json \
    --connection-type iotGreengrassV2 \
    --vehicle-name fwdemo \
    --can-bus0 vcan0
```

## Deployment

### Cloud Deployment

Deploy FWE via Greengrass deployment console. By creating a component using the example recipe like
[recipes/com.amazon.aws.IoTFleetWise-2.0.0.json](recipes/com.amazon.aws.IoTFleetWise-2.0.0.json).

### Local Deployment

Create the persistency path for FWE:

```bash
sudo mkdir -p /var/aws-iot-fleetwise
```

Make a local deployment of IoT FleetWise as a Greengrass Component by running the following:

```bash
cp build/aws-iot-fleetwise-edge \
    tools/greengrassV2/artifacts/com.amazon.aws.IoTFleetWise/1.0.0/ \
&& sudo /greengrass/v2/bin/greengrass-cli deployment create \
    --recipeDir tools/greengrassV2/recipes \
    --artifactDir tools/greengrassV2/artifacts \
    --merge "com.amazon.aws.IoTFleetWise=1.0.0"
```

View the logs of running component as follows:

```bash
sudo tail -f /greengrass/v2/logs/com.amazon.aws.IoTFleetWise.log
```

## Testing

To test IoT FleetWise you can now follow the instructions in the
[Edge Agent Developer Guide](../../docs/dev-guide/edge-agent-dev-guide.md#use-the-aws-iot-fleetwise-demo)
to run the cloud demo script.

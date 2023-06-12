# Tutorial: Run AWS IoT FleetWise Edge Agent on a Raspberry Pi

**Copyright © Amazon Web Services, Inc. and/or its affiliates. All rights reserved.**

Amazon's trademarks and trade dress may not be used in connection with any product or service that
is not Amazon's, in any manner that is likely to cause confusion among customers, or in any manner
that disparages or discredits Amazon. All other trademarks not owned by Amazon are the property of
their respective owners, who may or may not be affiliated with, connected to, or sponsored by
Amazon.

**Note**

- AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe (Frankfurt).
- The AWS IoT FleetWise in-vehicle software component is licensed to you under the Apache License,
  Version 2.0.
- You are solely responsible for ensuring such software and any updates and modifications thereto
  are deployed and maintained safely and securely in any vehicles and do not otherwise impact
  vehicle safety.

## Topics

- [Prerequisites](#prerequisites)
- [Step 1: Set up the Raspberry Pi](#step-1-setup-the-raspberry-pi)
- [Step 2: Launch your development machine](#step-2-launch-your-development-machine)
- [Step 3: Compile AWS IoT FleetWise Edge Agent software](#step-3-compile-aws-iot-fleetwise-edge-agent-software)
- [Step 4: Provision AWS IoT credentials](#step-4-provision-aws-iot-credentials)
- [Step 5: Deploy Edge Agent](#step-5-deploy-edge-agent)
- [Step 6: Deploy a campaign to the Raspberry Pi](#step-6-deploy-a-campaign-to-the-raspberry-pi)

## Prerequisites

- A Raspberry Pi, version 3 or later, (64-bit)
- An SD-card, with a minimum of 4 GB storage
- A CAN 'Hat' for Raspberry Pi with an MCP2515 CAN controller such as the
  [XYGStudy 2-Channel Isolated CAN Bus Expansion HAT](https://www.amazon.com/Raspberry-2-Channel-SN65HVD230-Protection-XYGStudy/dp/B087PWBFV8?th=1),
  [Coolwell Waveshare 2-Channel Isolated CAN Bus Expansion Hat](https://www.amazon.de/-/en/Waveshare-CAN-HAT-SN65HVD230-Protection/dp/B087PWNMM8/?th=1),
  or the
  [2-Channel Isolated CAN Bus Expansion HAT](https://rarecomponents.com/store/2-ch-can-hat-waveshare).
- Access to an AWS account with administrator permissions
- To be signed in to the AWS Management Console with an account in your chosen Region
  - Note: AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe (Frankfurt).
- A local Windows, Mac, or Linux machine

## Step 1: Setup the Raspberry Pi

1. Download Ubuntu 20.04 for Raspberry Pi
   (https://cdimage.ubuntu.com/ubuntu/releases/20.04/release/) on a local Windows, Mac, or Linux
   machine. Look for the "Raspberry Pi Generic (64-bit ARM) preinstalled server image".
1. To flash (write operating system image) to the SD card, use
   [Balena Etcher](https://www.balena.io/etcher/) (available for Windows, Mac and Linux).
1. Insert the SD card into your Raspberry Pi, attach the CAN hat, connect the Raspberry Pi to your
   internet router via an Ethernet cable, and turn on the power.
1. SSH to Raspberry Pi, using the initial password `ubuntu`: (Note: If connecting to the hostname
   `ubuntu` doesn't work, find the IP address from your internet router instead.)
   ```bash
   ssh ubuntu@ubuntu
   ```
1. Run the following to update the system and install `unzip`:
   ```bash
   sudo apt update \
     && sudo apt upgrade -y \
     && sudo apt install -y unzip
   ```
1. Run
   ```
   sudo nano /boot/firmware/usercfg.txt
   ```
   and add the following lines to enable the CAN hat:
   ```bash
   dtparam=spi=on
   dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=23
   dtoverlay=mcp2515-can1,oscillator=16000000,interrupt=25
   dtoverlay=spi-bcm2835-overlay
   ```
1. Save the file (`CTRL+O`, `CTRL+X`) and reboot the Raspberry Pi (`sudo reboot`).

## Step 2: Launch your development machine

These steps require an Ubuntu 20.04 development machine with 10 GB free disk space. If necessary,
you can use a local Intel x86_64 (amd64) machine. We recommended using the following instructions to
launch an AWS EC2 Graviton (arm64) instance. For more information about Amazon EC2 pricing, see
[Amazon EC2 On-Demand Pricing](https://aws.amazon.com/ec2/pricing/on-demand/).

### To launch an Amazon EC2 instance with administrator permissions

1. Sign in to your [AWS account](https://aws.amazon.com/console/).
1. Open the
   [**Launch CloudFormation Template**](https://us-east-1.console.aws.amazon.com/cloudformation/home?region=us-east-1#/stacks/quickcreate?templateUrl=https%3A%2F%2Faws-iot-fleetwise.s3.us-west-2.amazonaws.com%2Flatest%2Fcfn-templates%2Ffwdev.yml&stackName=fwdev&param_Ec2VolumeSize=20).
1. Enter the **Name** of an existing SSH key pair in your account from
   [here](https://us-east-1.console.aws.amazon.com/ec2/v2/home?region=us-east-1#KeyPairs:).
   - Don't include the file suffix `.pem`.
   - If you don't have an SSH key pair,
     [create one](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/create-key-pairs.html) and
     download the corresponding `.pem` file. Be sure to update the file permissions:
     `chmod 400 <PATH_TO_PEM>`
1. Select **I acknowledge that AWS CloudFormation might create IAM resources with custom names.**
1. Choose **Create stack**. Wait until the status of the Stack is **CREATE_COMPLETE**. This can take
   up to five minutes.
1. Choose the **Outputs** tab, copy the EC2 IP address, and connect from your local machine through
   SSH to the development machine.

   ```bash
   ssh -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>
   ```

## Step 3: Compile AWS IoT FleetWise Edge Agent software

Next, compile the AWS IoT FleetWise Edge Agent software for the ARM 64-bit architecture of the
processor present in the Raspberry Pi.

### To compile the AWS IoT FleetWise Edge Agent software

1. On your development machine, clone the latest AWS IoT FleetWise Edge Agent software from GitHub
   by running the following:

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
     && cd ~/aws-iot-fleetwise-edge
   ```

1. Install the AWS IoT FleetWise Edge Agent dependencies. The command below installs the following
   Ubuntu packages for compiling the Edge Agent for ARM 64-bit:

   `libssl-dev libboost-system-dev libboost-log-dev libboost-thread-dev build-essential cmake unzip git wget curl zlib1g-dev libcurl4-openssl-dev libsnappy-dev default-jre libasio-dev`.

   Additionally, it installs the following: `jsoncpp protobuf aws-sdk-cpp`. (If you are using a
   local x86_64 development machine, use the `install-deps-cross-arm64.sh` script instead.)

   ```bash
   sudo -H ./tools/install-deps-native.sh
   ```

1. To compile AWS IoT FleetWise Edge Agent software, run the following command. (If you are using a
   local x86_64 development machine, use the `build-fwe-cross-arm64.sh` script instead.)

   ```bash
   ./tools/build-fwe-native.sh
   ```

## Step 4: Provision AWS IoT credentials

On the development machine, create an IoT Thing with the name `fwdemo-rpi` and provision its
credentials by running the following command. The AWS IoT FleetWise Edge Agent binary and its
configuration files are packaged into a ZIP file that is ready for deployment to the Raspberry Pi.

```bash
mkdir -p ~/aws-iot-fleetwise-deploy && cd ~/aws-iot-fleetwise-deploy \
  && cp -r ~/aws-iot-fleetwise-edge/tools . \
  && mkdir -p build/src/executionmanagement \
  && cp ~/aws-iot-fleetwise-edge/build/src/executionmanagement/aws-iot-fleetwise-edge \
    build/src/executionmanagement/ \
  && mkdir -p config && cd config \
  && ../tools/provision.sh \
    --vehicle-name fwdemo-rpi \
    --certificate-pem-outfile certificate.pem \
    --private-key-outfile private-key.key \
    --endpoint-url-outfile endpoint.txt \
    --vehicle-name-outfile vehicle-name.txt \
  && ../tools/configure-fwe.sh \
    --input-config-file ~/aws-iot-fleetwise-edge/configuration/static-config.json \
    --output-config-file config-0.json \
    --log-color Yes \
    --vehicle-name `cat vehicle-name.txt` \
    --endpoint-url `cat endpoint.txt` \
    --can-bus0 can0 \
  && cd .. && zip -r aws-iot-fleetwise-deploy.zip .
```

## Step 5: Deploy Edge Agent

### To deploy AWS IoT FleetWise Edge Agent

1. On your local machine, copy the deployment ZIP file from the machine with Amazon EC2 to your
   local machine by running the following command:

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:aws-iot-fleetwise-deploy/aws-iot-fleetwise-deploy.zip .
   ```

1. On your local machine, copy the deployment ZIP file from your local machine to the Raspberry Pi
   by running the following command:

   ```bash
   scp aws-iot-fleetwise-deploy.zip ubuntu@ubuntu:
   ```

1. As described in step 4 of [setting up the Raspberry Pi](#step-1-setup-the-raspberry-pi), connect
   through SSH to the Raspberry Pi. On the Raspberry Pi, install AWS IoT FleetWise Edge Agent as a
   service by running the following command:

   ```bash
   mkdir -p ~/aws-iot-fleetwise-deploy && cd ~/aws-iot-fleetwise-deploy \
     && unzip -o ~/aws-iot-fleetwise-deploy.zip \
     && sudo mkdir -p /etc/aws-iot-fleetwise \
     && sudo cp config/* /etc/aws-iot-fleetwise \
     && sudo ./tools/install-fwe.sh
   ```

1. Install the [can-isotp](https://en.wikipedia.org/wiki/ISO_15765-2) module:
   ```
   sudo -H ~/aws-iot-fleetwise-deploy/tools/install-socketcan.sh
   ```
1. Run
   ```
   sudo nano /usr/local/bin/setup-socketcan.sh
   ```
   and add the following lines to bring up the `can0` and `can1` interfaces at startup:
   ```
   ip link set up can0 txqueuelen 1000 type can bitrate 500000 restart-ms 100
   ip link set up can1 txqueuelen 1000 type can bitrate 500000 restart-ms 100
   ```
1. Restart the setup-socketcan service and the IoT FleetWise Edge Agent service:
   ```
   sudo systemctl restart setup-socketcan
   sudo systemctl restart fwe@0
   ```
1. To verify the IoT FleetWise Edge Agent is running and is connected to the cloud, check the Edge
   Agent log files:
   ```
   sudo journalctl -fu fwe@0 --output=cat
   ```
   - Look for this message to verify:
     ```
     [INFO ] [AwsIotConnectivityModule.cpp:161] [connect()] [Connection completed successfully]
     ```
   - Use the
     [troubleshooting information and solutions](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/troubleshooting.html)
     in the AWS IoT FleetWise Developer Guide to help resolve issues with AWS IoT FleetWise Edge
     Agent.

## Step 6: Deploy a campaign to the Raspberry Pi

1. On the development machine, install the AWS IoT FleetWise cloud demo script dependencies by
   running the following commands. The script installs the following Ubuntu packages:
   `python3 python3-pip`, and then installs the following PIP packages:
   `wrapt plotly pandas cantools`.

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
     && sudo -H ./install-deps.sh
   ```

1. On the development machine, deploy a heartbeat campaign that periodically collects OBD data by
   running the following commands:

   ```bash
   ./demo.sh --vehicle-name fwdemo-rpi --campaign-file campaign-obd-heartbeat.json
   ```

   The demo script does the following:

   1. Registers your AWS account with AWS IoT FleetWise, if it's not already registered.
   1. Creates a signal catalog. First, the demo script adds standard OBD signals based on
      `obd-nodes.json`. Next, it adds CAN signals in a flat signal list based on the DBC file
      `hscan.dbc`.
   1. Creates a vehicle model, or _model manifest_, that references the signal catalog with every
      OBD and DBC signal.
   1. Activates the vehicle model.
   1. Creates a decoder manifest linked to the vehicle model using `obd-decoders.json` for decoding
      OBD signals from the network interfaces defined in `network-interfaces.json`.
   1. Imports the CAN signal decoding information from `hscan.dbc` to the decoder manifest.
   1. Updates the decoder manifest to set the status as `ACTIVE`.
   1. Creates a vehicle with an ID equal to `fwdemo-rpi`, which is also the name passed to
      `provision.sh`.
   1. Creates a fleet.
   1. Associates the vehicle with the fleet.
   1. Creates a campaign from `campaign-obd-heartbeat.json`. This contains a time-based collection
      scheme that collects OBD data and targets the campaign at the fleet.
   1. Approves the campaign.
   1. Waits until the campaign status is `HEALTHY`, which means the campaign was deployed to the
      fleet.
   1. Waits 30 seconds and then downloads the collected data from Amazon Timestream.
   1. Saves the data to an HTML file.

   If you enabled S3 upload destination by passing the option `--enable-s3-upload`, the demo script
   will additionally:

   - Create S3 bucket for collected data for S3 campaigns, if not already created
   - Create IAM roles and policies required for the service to write data to the S3 resources
   - Creates 2 additional campaigns from `campaign-brake-event.json`. One campaign will upload data
     to to S3 in JSON format, one to S3 in parquet format
   - Wait 20 minutes for the data to propagate to S3 and then downloads it
   - Save the data to an HTML file

   When the script completes, you receive the path to the output HTML file on your local machine. To
   download it, use `scp`, and then open it in your web browser:

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:<PATH_TO_HTML_FILE> .
   ```

1. To explore the collected data, click and drag on the graph to zoom in. Alternatively, if your AWS
   account is enrolled with QuickSight or Amazon Managed Grafana, you can use them to browse the
   data from Amazon Timestream directly.

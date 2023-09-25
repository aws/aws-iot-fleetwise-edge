# Raspberry Pi Tutorial

## Topics

- [Introduction](#introduction)
- [Prerequisites](#prerequisites)
- [Step 1: Set up the Raspberry Pi](#step-1-setup-the-raspberry-pi)
- [Step 2: Launch your development machine](#step-2-launch-your-development-machine)
- [Step 3: Compile Edge Agent](#step-3-compile-egde-agent)
- [Step 4: Provision AWS IoT credentials](#step-4-provision-aws-iot-credentials)
- [Step 5: Deploy Edge Agent](#step-5-deploy-edge-agent)
- [Step 6: Deploy a campaign to the Raspberry Pi](#step-6-deploy-a-campaign-to-the-raspberry-pi)
- [Step 7: Clean up](#step-7-clean-up)

**Copyright (C) Amazon Web Services, Inc. and/or its affiliates. All rights reserved.**

Amazon's trademarks and trade dress may not be used in connection with any product or service that
is not Amazon's, in any manner that is likely to cause confusion among customers, or in any manner
that disparages or discredits Amazon. All other trademarks not owned by Amazon are the property of
their respective owners, who may or may not be affiliated with, connected to, or sponsored by
Amazon.

## Introduction

**AWS IoT FleetWise** provides a set of tools that enable automakers to collect, transform, and
transfer vehicle data to the cloud at scale. With AWS IoT FleetWise you can build virtual
representations of vehicle networks and define data collection rules to transfer only high-value
data from your vehicles to AWS Cloud.

**The Reference Implementation for AWS IoT FleetWise ("FWE")** provides C++ libraries that can be
run with simulated vehicle data on certain supported vehicle hardware or that can help you develop
an Edge Agent to run an application on your vehicle that integrates with AWS IoT FleetWise. You can
use AWS IoT FleetWise pre-configured analytic capabilities to process collected data, gain insights
about vehicle health, and use the service's visual interface to help diagnose and troubleshoot
potential issues with the vehicle.

AWS IoT FleetWise's capability to collect ECU data and store them on cloud databases enables you to
utilize different AWS services, such as Analytics Services, and ML, to develop novel use-cases that
augment and/or supplement your existing vehicle functionality. In particular, AWS IoT FleetWise can
help utilize fleet data (Big Data) to create value. For example, you can develop use cases that
optimize vehicle routing, improve electric vehicle range estimation, and optimize battery life
charging. You can use the data ingested through AWS IoT FleetWise to develop applications for
predictive diagnostics, and for outlier detection with an electric vehicle's battery cells.

You can use the included sample C++ application to learn more about the Reference Implementation,
develop an Edge Agent for your use case and test interactions before integration.

This software is licensed under the
[Apache License, Version 2.0](http://www.apache.org/licenses/LICENSE-2.0).

### Disclaimer

**_The Reference Implementation for AWS IoT FleetWise ("FWE") is intended to help you develop your
Edge Agent for AWS IoT FleetWise and includes sample code that you may reference or modify so your
Edge Agent meets your requirements. As provided in the AWS IoT FleetWise Service Terms, you are
solely responsible for your Edge Agent, including ensuring that your Edge Agent and any updates and
modifications thereto are deployed and maintained safely and securely in any vehicles._**

**_This software code base includes modules that are still in development and are disabled by
default. These modules are not intended for use in a production environment. This includes a Remote
Profiler module that helps sending traces from the device to AWS Cloud Watch. FWE has been checked
for any memory leaks and runtime errors such as type overflows using Valgrind. No issues have been
detected during the load tests._**

**_Note that vehicle data collected through your use of AWS IoT FleetWise is intended for
informational purposes only (including to help you train cloud-based artificial intelligence and
machine learning models), and you may not use AWS IoT FleetWise to control or operate vehicle
functions. You are solely responsible for all liability that may arise in connection with any use
outside of AWS IoT FleetWise's intended purpose and in any manner contrary to applicable vehicle
regulations. Vehicle data collected through your use of AWS IoT FleetWise should be evaluated for
accuracy as appropriate for your use case, including for purposes of meeting any compliance
obligations you may have under applicable vehicle safety regulations (such as safety monitoring and
reporting obligations). Such evaluation should include collecting and reviewing information through
other industry standard means and sources (such as reports from drivers of vehicles). You and your
End Users are solely responsible for all decisions made, advice given, actions taken, and failures
to take action based on your use of AWS IoT FleetWise._**

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
  - **Note:** AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe
    (Frankfurt).
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

## Step 3: Compile Edge Agent

Next, compile FWE for the ARM 64-bit architecture of the processor present in the Raspberry Pi.

1. On your development machine, clone the latest FWE source code from GitHub by running the
   following:

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
   && cd ~/aws-iot-fleetwise-edge
   ```

1. Review, modify and supplement [the FWE source code](../../src/) to ensure it meets your use case
   and requirements.

1. Install the FWE dependencies. The command below installs the following Ubuntu packages for
   compiling FWEfor ARM 64-bit:

   `libssl-dev libboost-system-dev libboost-log-dev libboost-thread-dev build-essential cmake unzip git wget curl zlib1g-dev libcurl4-openssl-dev libsnappy-dev default-jre libasio-dev`.

   Additionally, it installs the following: `jsoncpp protobuf aws-sdk-cpp`. (If you are using a
   local x86_64 development machine, use the `install-deps-cross-arm64.sh` script instead.)

   ```bash
   sudo -H ./tools/install-deps-native.sh
   ```

1. To compile your Edge Agent, run the following command. (If you are using a local x86_64
   development machine, use the `build-fwe-cross-arm64.sh` script instead.)

   ```bash
   ./tools/build-fwe-native.sh
   ```

## Step 4: Provision AWS IoT credentials

On the development machine, create an IoT Thing with the name `fwdemo-rpi` and provision its
credentials by running the following command. Your Edge Agent binary and its configuration files are
packaged into a ZIP file that is ready for deployment to the Raspberry Pi.

```bash
mkdir -p ~/aws-iot-fleetwise-deploy \
&& cd ~/aws-iot-fleetwise-deploy \
&& cp -r ~/aws-iot-fleetwise-edge/tools . \
&& mkdir -p build \
&& cp ~/aws-iot-fleetwise-edge/build/aws-iot-fleetwise-edge build \
&& mkdir -p config \
&& cd config \
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
&& cd .. \
&& zip -r aws-iot-fleetwise-deploy.zip .
```

## Step 5: Deploy Edge Agent

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
   through SSH to the Raspberry Pi. On the Raspberry Pi, install your Edge Agent as a service by
   running the following command:

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
1. Restart the setup-socketcan service and your Edge Agent service:
   ```
   sudo systemctl restart setup-socketcan
   sudo systemctl restart fwe@0
   ```
1. To verify your Edge Agent is running and is connected to the cloud, check the log file:
   ```
   sudo journalctl -fu fwe@0 --output=cat
   ```
   - Look for this message to verify:
     ```
     [INFO ] [AwsIotConnectivityModule.cpp:161] [connect()] [Connection completed successfully]
     ```
   - Use the
     [troubleshooting information and solutions](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/troubleshooting.html)
     in the AWS IoT FleetWise Developer Guide to help resolve issues with FWE.

## Step 6: Deploy a campaign to the Raspberry Pi

1. Run the following _on the development machine_ to install the dependencies of the demo script:

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
   && sudo -H ./install-deps.sh
   ```

   The above command installs the following Ubuntu packages: `python3 python3-pip`. It then installs
   the following PIP packages: `wrapt plotly pandas cantools fastparquet`

1. Deploy a heartbeat campaign that periodically collects OBD data by running the following
   commands:

   ```bash
   ./demo.sh --vehicle-name fwdemo-rpi --campaign-file campaign-obd-heartbeat.json
   ```

   The demo script:

   1. Registers your AWS account with AWS IoT FleetWise, if not already registered.
   1. Creates an Amazon Timestream database and table.
   1. Creates IAM role and policy required for the service to write data to Amazon Timestream.
   1. Creates a signal catalog, firstly based on `obd-nodes.json` to add standard OBD signals, and
      secondly based on the DBC file `hscan.dbc` to add CAN signals in a flat signal list.
   1. Creates a model manifest that references the signal catalog with all of the OBD and DBC
      signals.
   1. Activates the model manifest.
   1. Creates a decoder manifest linked to the model manifest using `obd-decoders.json` for decoding
      OBD signals from the network interfaces defined in `network-interfaces.json`.
   1. Imports the CAN signal decoding information from `hscan.dbc` to the decoder manifest.
   1. Updates the decoder manifest to set the status as `ACTIVE`.
   1. Creates a vehicle with a name equal to `fwdemo-rpi`, the same as the name passed to
      `provision.sh`.
   1. Creates a fleet.
   1. Associates the vehicle with the fleet.
   1. Creates a campaign from `campaign-obd-heartbeat.json` that contains a time-based collection
      scheme that collects OBD data and targets the campaign at the fleet.
   1. Approves the campaign.
   1. Waits until the campaign status is `HEALTHY`, which means the campaign has been deployed to
      the fleet.
   1. Waits 30 seconds and then downloads the collected data from Amazon Timestream.
   1. Saves the data to an HTML file.

   This script will not delete Amazon Timestream resources.

1. When the script completes, a path to an HTML file is given. _On your local machine_, use `scp` to
   download it, then open it in your web browser:

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:<PATH_TO_HTML_FILE> .
   ```

1. To explore the collected data, you can click and drag to zoom in.

   Alternatively, if your AWS account is enrolled with Amazon QuickSight or Amazon Managed Grafana,
   you may use them to browse the data from Amazon Timestream directly.

## Step 7: Clean up

Run the following to clean up resources created by the `provision.sh` and `demo.sh` scripts.
**Note:** The Amazon Timestream resources are not deleted.

```bash
cd ~/aws-iot-fleetwise-edge/tools/cloud \
&& clean-up.sh \
&& ../provision.sh \
   --vehicle-name fwdemo-rpi \
   --region us-east-1 \
   --only-clean-up
```

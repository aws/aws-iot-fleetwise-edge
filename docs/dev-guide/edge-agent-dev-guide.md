# Edge Agent Developer Guide

**Note:** AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe (Frankfurt).

**Topics**

- [Introduction](#introduction)
- [Quick start demo](#quick-start-demo)
- [Getting started guide](#getting-started-guide)
- [Software Architecture](#software-architecture)

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

# Quick start demo

This guide is intended to quickly demonstrate the basic features of AWS IoT FleetWise by firstly
deploying FWE to an AWS EC2 instance representing one or more simulated vehicles. A script is then
run using the AWS CLI to control AWS IoT FleetWise in order collect data from the simulated vehicle.

This guide showcases AWS IoT FleetWise at a high level. If you are interested in exploring AWS IoT
FleetWise at a more detailed technical level, see the
[Getting started guide](#getting-started-guide).

**Topics:**

- [Prerequisites for quick start demo](#prerequisites-for-quick-start-demo)
- [Deploy Edge Agent](#deploy-edge-agent)
- [Use the AWS IoT FleetWise demo](#use-the-aws-iot-fleetwise-demo)
- [Clean up resources](#clean-up-resources)

## Prerequisites for quick start demo

- Access to an AWS Account with administrator privileges.
- Logged in to the AWS Console in your desired region using the account with administrator
  privileges.
  - **Note:** AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe
    (Frankfurt).

## Deploy Edge Agent

After reviewing [the FWE source code](../../src/), to ensure that it meets your use case and
requirements, you can use the following CloudFormation template to deploy FWE to a new AWS EC2
instance.

1. Click here to
   [**Launch CloudFormation Template**](https://us-east-1.console.aws.amazon.com/cloudformation/home?region=us-east-1#/stacks/quickcreate?templateUrl=https%3A%2F%2Faws-iot-fleetwise.s3.us-west-2.amazonaws.com%2Flatest%2Fcfn-templates%2Ffwdemo.yml&stackName=fwdemo).
1. (Optional) You can increase the number of simulated vehicles by updating the `FleetSize`
   parameter. You can also specify the region IoT Things are created in by updating the
   `IoTCoreRegion` parameter.
1. Select the checkbox next to _'I acknowledge that AWS CloudFormation might create IAM resources
   with custom names.'_
1. Choose **Create stack**.
1. Wait until the status of the Stack is 'CREATE_COMPLETE', this will take approximately 10 minutes.

FWE has been deployed to an AWS EC2 Graviton (ARM64) Instance along with credentials that allow it
to connect to AWS IoT Core. CAN data is also being generated on the EC2 instance to simulate
periodic hard-braking events. The AWS IoT FleetWise demo script in the following section will deploy
a campaign to the simulated fleet of vehicles to capture the engine torque when a hard braking-event
occurs.

## Use the AWS IoT FleetWise demo

The instructions below will register your AWS account for AWS IoT FleetWise, create a demonstration
vehicle model, register the virtual vehicle created in the previous section and run a campaign to
collect data from it.

1. Open the AWS CloudShell: [Launch CloudShell](https://console.aws.amazon.com/cloudshell/home)
1. Copy and paste the following commands to clone the latest FWE software from GitHub and install
   the dependencies of the demo script.

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
   && cd ~/aws-iot-fleetwise-edge/tools/cloud \
   && sudo -H ./install-deps.sh
   ```

   The above command installs the following PIP packages: `wrapt plotly pandas cantools fastparquet`

1. If you are using the AWS CLI with a version lower than v2.11.24, update the CLI by running:

   ```bash
   curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip" \
   && unzip -q awscliv2.zip \
   && sudo ./aws/install --update \
   && rm -rf ./aws*
   ```

1. Run the demo script:

   ```bash
   ./demo.sh --vehicle-name fwdemo
   ```

   - (Optional) To enable S3 upload, append the option `--enable-s3-upload`

     ```bash
     ./demo.sh --vehicle-name fwdemo --enable-s3-upload
     ```

   - (Optional) If you selected a `FleetSize` of greater than one above, append the option
     `--fleet-size <SIZE>`, where `<SIZE>` is the number selected.
   - (Optional) If you changed `IoTCoreRegion` above, append the option `--region <REGION>`, where
     `<REGION>` is the selected region.
   - (Optional) If you changed `Stack name` when creating the stack above, pass the new stack name
     to the `--vehicle-name` option.

     For example, if you chose to create two AWS IoT things in Europe (Frankfurt) with a stack named
     `myfwdemo`, you must pass those values when calling `demo.sh`:

     ```bash
     ./demo.sh --vehicle-name myfwdemo --fleet-size 2 --region eu-central-1
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
   1. Creates a vehicle with a name equal to `fwdemo` which is the same as the name given to the
      CloudFormation Stack name in the previous section.
   1. Creates a fleet.
   1. Associates the vehicle with the fleet.
   1. Creates a campaign from `campaign-brake-event.json` that contains a condition-based collection
      scheme to capture the engine torque and the brake pressure when the brake pressure is above
      7000, and targets the campaign at the fleet.
   1. Approves the campaign.
   1. Waits until the campaign status is `HEALTHY`, which means the campaign has been deployed to
      the fleet.
   1. Waits 30 seconds and then downloads the collected data from Amazon Timestream.
   1. Saves the data to an HTML file.

   If S3 upload is enabled, the demo script will additionally:

   1. Create an S3 bucket with a bucket policy that allows AWS IoT FleetWise to write data to the
      bucket.
   1. Creates 2 additional campaigns from `campaign-brake-event.json`. One campaign will upload data
      to to S3 in JSON format, one to S3 in parquet format.
   1. Wait 20 minutes for the data to propagate to S3 and then download it.
   1. Save the data to an HTML file.

   This script will not delete Amazon Timestream or S3 resources.

1. When the script completes, a path to an HTML file is given. Copy the path, then click on the
   Actions drop down menu in the top-right corner of the CloudShell window and choose **Download
   file**. Paste the path to the file, choose **Download**, and open the downloaded file in your
   browser.

1. To explore the collected data, you can click and drag to zoom in. The red line shows the
   simulated brake pressure signal. As you can see that when hard braking events occur (value above
   7000), collection is triggered and the engine torque signal data is collected.

   Alternatively, if your AWS account is enrolled with Amazon QuickSight or Amazon Managed Grafana,
   you may use them to browse the data from Amazon Timestream directly.

   ![](./images/collected_data_plot.png)

## Clean up resources

Copy and paste the following commands to AWS CloudShell to clean up resources created by the
`provision.sh` and `demo.sh` scripts. **Note:** The Amazon Timestream and S3 resources are not
deleted.

```bash
cd ~/aws-iot-fleetwise-edge/tools/cloud \
&& clean-up.sh \
&& ../provision.sh \
   --vehicle-name fwdemo \
   --region us-east-1 \
   --only-clean-up
```

# Getting started guide

This guide is intended to demonstrate the basic features of AWS IoT FleetWise by firstly allowing
you to build your own Edge Agent and running it on a development machine (locally or on AWS EC2) in
order to represent a simulated vehicle. A script is then run to interact with AWS IoT FleetWise in
order to collect data from the simulated vehicle. Instructions are also provided for running your
own Edge Agent on an NXP S32G-VNP-RDB2 development board or Renesas R-Car S4 Spider board and
deploying a campaign to collect OBD data.

This guide covers building your Edge Agent at a detailed technical level, using a development
machine to build and run the executable. If you would prefer to learn about AWS IoT FleetWise at a
higher level that does not require use of a development machine, see the
[Quick start demo](#quick-start-demo).

#### Topics:

- [Getting started on a development machine](#getting-started-on-a-development-machine)
  - [Prerequisites for development machine](#prerequisites-for-development-machine)
  - [Launch your development machine](#launch-your-development-machine)
  - [Compile your Edge Agent](#compile-your-edge-agent)
  - [Deploy your Edge Agent](#deploy-your-edge-agent)
  - [Run the AWS IoT FleetWise demo script](#run-the-aws-iot-fleetwise-demo-script)
  - [Clean up](#clean-up)
- [Getting started on an NXP S32G board](./edge-agent-dev-guide-nxp-s32g.md)
  - [Prerequisites for NXP S32G](./edge-agent-dev-guide-nxp-s32g.md#prerequisites)
  - [Build an SD-Card Image](./edge-agent-dev-guide-nxp-s32g.md#build-an-sd-card-image)
  - [Flash the SD-Card Image](./edge-agent-dev-guide-nxp-s32g.md#flash-the-sd-card-image)
  - [Specify initial board configuration](./edge-agent-dev-guide-nxp-s32g.md#specify-initial-board-configuration)
  - [Provision AWS IoT Credentials](./edge-agent-dev-guide-nxp-s32g.md#provision-aws-iot-credentials)
  - [Deploy Edge Agent on NXP S32G board](./edge-agent-dev-guide-nxp-s32g.md#deploy-edge-agent-on-nxp-s32g-board)
  - [Collect OBD Data](./edge-agent-dev-guide-nxp-s32g.md#collect-obd-data)
  - [Clean up](./edge-agent-dev-guide-nxp-s32g.md#clean-up)
- [Getting started on a Renesas R-Car S4 board](./edge-agent-dev-guide-renesas-rcar-s4.md)
  - [Prerequisites](./edge-agent-dev-guide-renesas-rcar-s4.md#prerequisites)
  - [Build an SD-Card Image](./edge-agent-dev-guide-renesas-rcar-s4.md#build-an-sd-card-image)
  - [Flash the SD-Card Image](./edge-agent-dev-guide-renesas-rcar-s4.md#flash-the-sd-card-image)
  - [Specify initial board configuration](./edge-agent-dev-guide-renesas-rcar-s4.md#specify-initial-board-configuration)
  - [Provision AWS IoT Credentials](./edge-agent-dev-guide-renesas-rcar-s4.md#provision-aws-iot-credentials)
  - [Deploy Edge Agent on R-Car S4 Spider board](./edge-agent-dev-guide-renesas-rcar-s4.md#deploy-edge-agent-on-r-car-s4-spider-board)
  - [Collect OBD Data](./edge-agent-dev-guide-renesas-rcar-s4.md#collect-obd-data)
  - [Clean up](./edge-agent-dev-guide-renesas-rcar-s4.md#clean-up)

## Getting started on a development machine

This section describes how to get started on a development machine.

### Prerequisites for development machine

- Access to an AWS Account with administrator privileges.
- Logged in to the AWS Console in your desired region using the account with administrator
  privileges.
  - **Note:** AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe
    (Frankfurt).
- A local Linux or MacOS machine.

### Launch your development machine

An Ubuntu 20.04 development machine with 200GB free disk space will be required. A local Intel
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

### Compile your Edge Agent

1. Run the following _on the development machine_ to clone the latest FWE source code from GitHub.

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
   && cd ~/aws-iot-fleetwise-edge
   ```

1. Review, modify and supplement [the FWE source code](../../src/) to ensure it meets your use case
   and requirements.

1. Install the dependencies for FWE by running the commands below.

   ```bash
   sudo -H ./tools/install-deps-native.sh \
   && sudo -H ./tools/install-socketcan.sh \
   && sudo -H ./tools/install-cansim.sh
   ```

   The commands above will:

   1. Install the following Ubuntu packages:
      `libssl-dev libboost-system-dev libboost-log-dev libboost-thread-dev build-essential cmake unzip git wget curl zlib1g-dev libcurl4-openssl-dev libsnappy-dev default-jre libasio-dev`.
      Additionally it installs the following: `jsoncpp protobuf aws-sdk-cpp`
   1. Install the following Ubuntu packages:
      `build-essential dkms can-utils git linux-modules-extra-aws`. Additionally it installs the
      following: `can-isotp`. It also installs a systemd service called `setup-socketcan` that
      brings up the virtual SocketCAN interface `vcan0` at startup.
   1. Install the following Ubuntu packages: `python3 python3-pip`. It then installs the following
      PIP packages: `wrapt cantools prompt_toolkit python-can can-isotp matplotlib`. It also
      installs a systemd service called `cansim` that periodically transmits data on the virtual
      SocketCAN bus `vcan0` to simulate vehicle data.

1. Run the following to compile your own Edge Agent:

   ```bash
   ./tools/build-fwe-native.sh
   ```

### Deploy your Edge Agent

1. Run the following _on the development machine_ to provision an AWS IoT Thing with credentials and
   install your Edge Agent as a service.

   **Note** To create AWS IoT things in Europe (Frankfurt), configure `--region` to `eu-central-1`
   in the call to `provision.sh`

   ```bash
   sudo mkdir -p /etc/aws-iot-fleetwise \
   && sudo ./tools/provision.sh \
      --vehicle-name fwdemo-ec2 \
      --certificate-pem-outfile /etc/aws-iot-fleetwise/certificate.pem \
      --private-key-outfile /etc/aws-iot-fleetwise/private-key.key \
      --endpoint-url-outfile /etc/aws-iot-fleetwise/endpoint.txt \
      --vehicle-name-outfile /etc/aws-iot-fleetwise/vehicle-name.txt \
   && sudo ./tools/configure-fwe.sh \
      --input-config-file configuration/static-config.json \
      --output-config-file /etc/aws-iot-fleetwise/config-0.json \
      --log-color Yes \
      --vehicle-name `cat /etc/aws-iot-fleetwise/vehicle-name.txt` \
      --endpoint-url `cat /etc/aws-iot-fleetwise/endpoint.txt` \
      --can-bus0 vcan0 \
   && sudo ./tools/install-fwe.sh
   ```

   1. At this point your Edge Agent is running and periodically sending 'checkins' to AWS IoT
      FleetWise, in order to announce its current list of campaigns (which at this stage will be an
      empty list). CAN data is also being generated on the development machine to simulate periodic
      hard-braking events. The AWS IoT FleetWise demo script in the following section will deploy a
      campaign to the simulated vehicle to capture the engine torque when a hard braking-event
      occurs.

1. Run the following to view and follow the log for your Edge Agent. You can open a new SSH session
   with the development machine and run this command to follow the log in real time as the campaign
   is deployed in the next section. To exit the logs, use CTRL + C.

   ```bash
   sudo journalctl -fu fwe@0 --output=cat
   ```

### Run the AWS IoT FleetWise demo script

The instructions below will register your AWS account for AWS IoT FleetWise, create a demonstration
vehicle model, register the virtual vehicle created in the previous section and run a campaign to
collect data from it.

1. Run the following _on the development machine_ to install the dependencies of the demo script:

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
   && sudo -H ./install-deps.sh
   ```

   The above command installs the following Ubuntu packages: `python3 python3-pip`. It then installs
   the following PIP packages: `wrapt plotly pandas cantools fastparquet`

1. If you are using the AWS CLI with a version lower than v2.11.24, update the CLI by running:

   ```bash
   curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip" \
   && unzip -q awscliv2.zip \
   && sudo ./aws/install --update \
   && rm -rf ./aws*
   ```

1. Run the following to explore the AWS IoT FleetWise CLI:

   ```bash
   aws iotfleetwise help
   ```

1. Run the demo script:

   ```bash
   ./demo.sh --vehicle-name fwdemo-ec2
   ```

   - (Optional) To enable S3 upload, append the option `--enable-s3-upload`

     ```bash
     ./demo.sh --vehicle-name fwdemo-ec2 --enable-s3-upload
     ```

   - (Optional) If you changed the `--region` option to `provision.sh` above, append the option
     `--region <REGION>`, where `<REGION>` is the selected region. For example, if you chose to
     create the AWS IoT thing in Europe (Frankfurt), you must configure `--region` to `eu-central-1`
     in the demo.sh file.

     ```bash
     ./demo.sh --vehicle-name fwdemo-ec2 --region eu-central-1
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
   1. Creates a vehicle with a name equal to `fwdemo-ec2`, the same as the name passed to
      `provision.sh`.
   1. Creates a fleet.
   1. Associates the vehicle with the fleet.
   1. Creates a campaign from `campaign-brake-event.json` that contains a condition-based collection
      scheme to capture the engine torque and the brake pressure when the brake pressure is above
      7000, and targets the campaign at the fleet.
   1. Approves the campaign.
   1. Waits until the campaign status is `HEALTHY`, which means the campaign has been deployed to
      the fleet.
   1. Waits 30 seconds and then downloads the collected data from Amazon Timestream.
   1. Saves the data to an HTML file.

   If S3 upload is enabled, the demo script will additionally:

   1. Create an S3 bucket with a bucket policy that allows AWS IoT FleetWise to write data to the
      bucket.
   1. Creates 2 additional campaigns from `campaign-brake-event.json`. One campaign will upload data
      to to S3 in JSON format, one to S3 in parquet format.
   1. Wait 20 minutes for the data to propagate to S3 and then download it.
   1. Save the data to an HTML file.

   This script will not delete Amazon Timestream or S3 resources.

1. When the script completes, a path to an HTML file is given. _On your local machine_, use `scp` to
   download it, then open it in your web browser:

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:<PATH_TO_HTML_FILE> .
   ```

1. To explore the collected data, you can click and drag to zoom in. The red line shows the
   simulated brake pressure signal. As you can see that when hard braking events occur (value above
   7000), collection is triggered and the engine torque signal data is collected.

   Alternatively, if your AWS account is enrolled with Amazon QuickSight or Amazon Managed Grafana,
   you may use them to browse the data from Amazon Timestream directly.

   ![](./images/collected_data_plot.png)

1. Run the following _on the development machine_ to deploy a 'heartbeat' campaign that collects OBD
   data from the vehicle. Repeat the process above to view the collected data.

   ```bash
   ./demo.sh --vehicle-name fwdemo-ec2 --campaign-file campaign-obd-heartbeat.json
   ```

   Similarly, if you chose to deploy the 'heartbeat' campaign that collects OBD data from an AWS IoT
   thing created in in Europe (Frankfurt), you must configure `--region`:

   ```bash
   ./demo.sh --vehicle-name fwdemo-ec2 --campaign-file campaign-obd-heartbeat.json --region eu-central-1
   ```

1. Run the following _on the development machine_ to import your custom DBC file. You also have to
   provide your custom campaign file. There is no support of simulation of custom signals so you
   have to test data collection with the real vehicle or custom simulator.

   ```bash
   ./demo.sh --vehicle-name fwdemo-ec2 --dbc-file <DBC_FILE> --campaign-file <CAMPAIGN_FILE>
   ```

   Similarly, if you chose to deploy the 'heartbeat' campaign that collects OBD data from an AWS IoT
   thing created in in Europe (Frankfurt), you must configure `--region`:

   ```bash
   ./demo.sh --vehicle-name fwdemo-ec2 --dbc-file <DBC_FILE> --campaign-file <CAMPAIGN_FILE> --region eu-central-1
   ```

### Clean up

Run the following _on the development machine_ to clean up resources created by the `provision.sh`
and `demo.sh` scripts. **Note:** The Amazon Timestream and S3 resources are not deleted.

```bash
cd ~/aws-iot-fleetwise-edge/tools/cloud \
&& clean-up.sh \
&& ../provision.sh \
   --vehicle-name fwdemo-ec2 \
   --region us-east-1 \
   --only-clean-up
```

## Getting started on a NXP S32G board

[Getting started on an NXP S32G board](./edge-agent-dev-guide-nxp-s32g.md)

## Getting started on an Renesas S4 board

[Getting started on a Renesas R-Car S4 board](./edge-agent-dev-guide-renesas-rcar-s4.md)

# Software Architecture

AWS IoT FleetWise is an AWS service that enables automakers to collect, store, organize, and monitor
data from vehicles. Automakers need the ability to connect remotely to their fleet of vehicles and
collect vehicle ECU and sensor data. AWS IoT FleetWise can be used by OEM engineers and data
scientists to build vehicle models that can be used to create custom data collection schemes. These
data collection schemes enable OEMs to optimize the data collection process by defining what signals
to collect, how often to collect them, and most importantly the trigger conditions, or events, that
enable the collection process.

This document reviews the architecture, operation, and key features of the Reference Implementation
for AWS IoT FleetWise ("FWE").

**Topics**

- [Terminology](#terminology)
- [Audience](#audience)
- [Architecture Overview](#architecture-overview)
  - [User Flow](#user-flow)
  - [Software Layers](#software-layers)
  - [Overview of the software libraries](#overview-of-the-software-libraries)
- [Programming and execution model](#programming-and-execution-model)
- [Data models](#data-models)
  - [Device to cloud communication](#device-to-cloud-communication)
  - [Cloud to device communication](#cloud-to-device-communication)
- [Data persistency](#data-persistency)
- [Logging](#logging)
- [Configuration](#configuration)
- [Security](#security)
  - [Best practices and recommendation](#best-practices-and-recommendation)
- [Supported platforms](#supported-platforms)
- [Getting help](#getting-help)
- [Resources](#resources)

## Terminology

**Decoder Manifest:** A configuration used for decoding raw vehicle data into physical measurements.
For example, CAN DBC decodes raw data from CAN Bus frames into physical signals like EngineRPM,
EngineSpeed, and RadarAmplitude.

**Data Collection Scheme:** A document that defines the rules used to collect data from a vehicle.
It defines an event trigger, filters, duration and frequency of data collection. This document is
used by FWE for collecting and filtering only relevant data. A data collection scheme can use
multiple event triggers. It can be attached to a single vehicle or a fleet of vehicles.

**Board Support Package (BSP):** A set of libraries that support running FWE on a POSIX Operating
System.

**Controller Area Network (CAN):** A serial networking technology that enables vehicle electronic
devices to interconnect.

**On Board Diagnostics (OBD):** A protocol used to retrieve vehicle diagnostics information.

**Message Queuing Telemetry Transport (MQTT)**

**Transport Layer Security (TLS)**

**Original Equipment Manufacturer (OEM)**

## Audience

This document is intended for System and Software Engineers at OEMs and System Integrators who are
developing or integrating in-vehicle software. Knowledge of C/C++, POSIX APIs, in-vehicle Networking
protocols (such as CAN) and external connectivity protocols (such as MQTT) are pre-requisites.

## Architecture Overview

The below figure outlines AWS IoT FleetWise system elements:

![](./images/architecture.png)

### User Flow

You can use the AWS IoT FleetWise Console to define a vehicle model, which consists of creating a
semantic digital twin of the vehicle. The semantic twin includes the vehicle attributes such as
model year, engine type, and the signal catalog of the vehicle. This vehicle description serves as a
foundation to define data collection schemes and specify what data to collect and which collection
triggers to inspect data.

AWS IoT FleetWise enables you to create campaigns that can be deployed to a fleet of vehicles. Once
a campaign is active, it is deployed from the cloud to the target vehicles via a push mechanism. FWE
uses the underlying collection schemes to acquire sensor data from the vehicle network. It applies
the inspection rules and uploads the data back to AWS IoT FleetWise data plane. The data plane
persists collected data in the OEM's AWS Account; the account can then be used to analyse the data.

### Software Layers

The functionality of FWE is dependent on a set of software layers that are interdependent. The
software layers exchange over abstract APIs that implement a dependency inversion design pattern.
The below section describes each of the layers bottom up as outline in the figure above.

**Vehicle Data Acquisition**

This layer binds FWE with the vehicle network, leveraging the onboard device drivers and
middle-wares the OEM uses in the target hardware. It has a dependency on the host environment
including the operating system, the peripheral drivers and the vehicle architecture overall. This
layer listens and/or requests data from the vehicle network and normalizes it, before applying
various signal decoding rules that are vehicle protocol specific e.g. CAN Signal database files. The
output of this layer is a set of transformed key/value pairs of signals and their corresponding
values. This layer has a dependency on the decoding rules the OEM has defined for the signals they
want to inspect, which are specified in the Cloud Control plane. This layer stores the decoded
values into a signal history buffer. This signal history buffer has a maximum fixed size to not
exhaust the system resources.

**Vehicle Data Inspection**

This layer of the software operates on the signal history buffer by applying the inspection rules.
An inspection rule ties one or more signal value to a set of conditions. If the condition is met, a
snapshot of the data at hand is shared with the Cloud data plane. More than one inspection rule can
be defined and applied to the signal data. This layer holds a set of data structures that allow fast
indexing of the data by identifier and time, so that the data collection can be achieved as a quick
as the conditions are met. Once a snapshot of the signal data is available, this layer passes over
the data to the offboard connectivity layer for further processing. The mechanism of data
transmission between these two layers uses also a message queue with a predefined maximum size.

**Collection Scheme Management**

The Cloud control plane serves FWE with data collection scheme and decoder manifests. A decoder
manifest is an artifact that defines the vehicle signal catalog and the way each signal can get
decoded from its raw format. A collection scheme describes the inspection rules to be applied to the
signal values. The Scheme Management module has the responsibility of managing the life cycle e.g.
activation/deactivation based on time and version of the collection schemes and the decoder manifest
delivered to FWE. The outcome of the scheme management are at any given point in time, the
inspection matrix needed by the Data Inspection Layer and the decoding dictionary needed by by the
Data Normalization layer.

**Offboard Connectivity**

This layer of the software owns the communication of FWE with the outside world i.e. AWS Cloud. It
implements a publish/subscribe pattern, with two communication channels, one used for receiving
collection schemes and decoder manifest from the Control Plane, and one for publishing the collected
data back to the Data Plane.

This layer ensures that FWE has valid credentials to communicate securely with the Cloud APIs.

**Execution Management**

This layer owns the execution context of FWE within the target hardware in the vehicle. It manages
the lifecycle of FWE, including the startup/shutdown sequences, along with acting as a local
monitoring module to ensure smooth execution of the application. The configuration of the
application is validated and loaded into the system in this layer of the software.

### Overview of the software modules

The code base of the Reference Implementation consists of C++ modules that implement the
functionalities of the layers described above. All these modules are compiled into a POSIX user
space application running a single process.

**BSP Modules**

```
CacheAndPersist
ClockHandler
ConsoleLogger
CPUUsageInfo
LoggingModule
MemoryUsageInfo
Thread
TraceModule
```

These modules include a set of APIs and utility functions that the rest of the system use to:

- Create and manage platform threads via a Thread and Signal APIs.
- Create and manage timers and clocks via a Timer and Clock APIs.
- Create loggers and metrics via Trace and logger APIs
- Monitor the CPU, IO and RAM usage of a module via CPU/IO/RAM utility functions.
- Persist data into a storage location via a Persistency API. This is used to persist and reload
  collection schemes and decoder manifest during shutdown and startup of the application.

These modules are used uniformly by all the other modules in the application.

**Vehicle Network Management Modules**

```
ISOTPOverCANReceiver
ISOTPOverCANSender
ISOTPOverCANSenderReceiver
```

These modules implement a set of wrappers around the in vehicle network communication protocols, and
realize the function of vehicle data acquisition. These modules include:

- An implementation of the Linux CAN APIs, to acquire standard CAN Traffic from the network using
  raw sockets.
- An implementation of the Linux ISO/TP over CAN APIs to acquire CAN Frames that are bigger than 8
  Bytes e.g. Diagnostic frames.

Each of the CAN Interfaces configured in the system will have a dedicated socket open. For the
Diagnostic session i.e. to request OBD II PIDs, a separate socket is open for writing and reading
CAN Frames. These modules abstract away all the Socket and Linux networking details from the rest of
the system, and expose only a circular buffer for each network interface configured, exposing the
raw CAN Frames to be consumed by the Data Inspection Modules.

**Data Management Modules**

```
CANDecoder
CheckinAndPersistency
CollectionSchemeIngestion
CollectionSchemeIngestionList
CollectionSchemeManager
DataSenderManager
DataSenderManagerWorkerThread
DataSenderProtoWriter
DecoderDictionaryExtractor
DecoderManifestIngestion
Geohash
InspectionMatrixExtractor
OBDDataDecoder
Schema
```

These modules implement all raw data decoders. It offers a Raw CAN Data Decoder (Standard CAN), an
OBD II (according to J1979 specification) decoder. Additionally, it implements the decoders for the
Collection Schemes and Decoder manifests.

These modules are used by the Data Inspection Modules to normalize and decode the raw CAN Frames,
and by the Execution Management Modules to initiate the Collection Scheme and decoder Manifest
decoding.

These modules also implement a serialization module to serialize the data FWE wants to send to the
data plane. The serialization schema is described below in the data model.

**Data Inspection Modules**

```
CANDataConsumer
CANDataSource
CollectionInspectionEngine
CollectionInspectionWorkerThread
ExternalCANDataSource
GeohashFunctionNode
OBDOverCANECU
OBDOverCANModule
```

These modules implement each of the following:

- Consume, decode and normalize of the CAN Raw Frames according to the Decoder Manifest available in
  the system.
- Consumer, decode and normalize of the ISO/TP CAN Frames (Diagnostic data) according to the Decoder
  Manifest available in the system.
- Filter and inspect the signals values decoded from the network according to the inspection rules
  provided in the Inspection Matrix.
- Cache of the needed signals in a signal history buffer.

Upon fulfillment of one or more trigger conditions, these modules extract from the signal history
buffer a data snapshot that's shared with the offboard connectivity modules for further processing.
Again here a circular buffer is used as a transport mechanism of the data.

**Offboard Connectivity Modules**

```
AwsBootstrap
AwsGGChannel
AwsGGConnectivityModule
AwsIotChannel
AwsIotConnectivityModule
AwsSDKMemoryManager
PayloadManager
RetryThread
RemoteProfiler
```

These modules implement the communication routines between FWE and the cloud Control Plane and Data
Plane.

Since all the communication between the device and the cloud occurs over a secure MQTT connection,
these modules uses the AWS SDK for C++ as an MQTT client. It creates exactly one connection to the
MQTT broker.

These modules then publish the data snapshot through that connection (through a dedicated MQTT
topic) and subscribes to the Scheme and decoder manifest topic (dedicated MQTT topic) for eventual
updates. On the subscribe side, these modules notifies the rest of the system on the arrival of an
update of either the Scheme or the decoder manifests, which are enacted accordant in near real time.

**Execution Management Module**

```
IoTFleetWiseEngine
```

This module implements the bootstrap sequence of FWE. It parses the provided configuration and
ensures that are the other modules are provided with their corresponding settings. During shutdown,
it ensures that all the modules and corresponding system resources (threads, loggers, and sockets)
are stopped/closed properly.

If there is no connectivity, or during shutdown of the application, this module persists the data
snapshots that are queued for sending to the cloud. Upon re-connection, this module will attempt to
send the data to the cloud. The `CacheAndPersist` module ensures that the disk space is not
exhausted, so this module just invokes the persistency module when it wants to read or write data to
disk.

## Programming and Execution model

FWE implements a concurrent and event-based multithreading system.

- **In the Vehicle Network Management Modules,** each CAN Network Interface spawns one thread to
  take care of opening a Socket to the network device. E.g. if the device has 4 CAN Networks
  configured, there will be one thread per network mainly doing message reception from the network
  and insertion into the corresponding circular buffer in a lock free fashion. Each of the threads
  raises a notification via a listener interface in case the underlying socket communication is
  interrupted. These threads operate in a polling mode i.e. wait on socket read in non blocking
  mode.
- **In the Data Management Modules**, one thread is created that manages the life cycle of the
  collection Scheme and decode manifest. This thread is busy waiting and wakes up only during an
  arrival of new collection schemes/manifest from the cloud, or during the expiry of any of the
  collection schemes in the system.
- **In the Data Inspection Modules**, each of the following modules spawn threads:
  - The inspection rule engine that does active inspection of the signals having one thread.
  - One thread for each CAN Network consuming the data and decoding/normalizing, working in polling
    mode, and using a lock free container to read and write CAN Messages.
    [A boost spsc](https://theboostcpplibraries.com/boost.lockfree) queue is used for this purpose.
  - One thread taking care of controlling the health of the Network Interfaces (event based) and
    shutting down the sockets if a CAN IF is interrupted.
  - One thread that does run a Diagnostic session at a given time frequency (running J1979 PID and
    DTC request)
- **In the Connectivity Modules**, most of the execution runs in the context of the main application
  thread. Callbacks and notifications from the MQTT stack happen in the context of the AWS IoT
  Device SDK thread. This module intercepts the notifications and switches the thread context to one
  of FWE threads so that no real data processing happens in the MQTT connection thread.
- **In the Execution Management Modules**, there are two threads
  - One thread which is managing all the bootstrap sequence and intercepting SystemD signals i.e.
    application main thread
  - One orchestration thread: This thread acts as a shadow for the MQTT thread i.e. swallows
    connectivity errors and invokes the persistency module. On each run cycle it retries to send the
    data in the persistency location.

## Data Models

FWE defines four schemas that describe the communication with the Cloud Control and Data Plane
services.

All the payloads exchanged between FWE and the cloud services are serialized in a Protobuff format.

### Device to Cloud communication

FWE sends two artifacts with the Cloud services:

**Check-in Information**

This check-in information consists of data collection schemes and decoder manifest Amazon Resource
Name (ARN) that are active in FWE at a given time point. This check-in message is send regularly at
a configurable frequency to the cloud services. Refer to
[checkin.proto](../../interfaces/protobuf/schemas/edgeToCloud/checkin.proto).

**Data Snapshot Information**

This message is send conditionally to the cloud data plane services once one or more inspection rule
is met. Depending on the configuration of FWE, (e.g. send decoded and raw data), FWE sends one or
more instance of this message in an MQTT packet to the cloud. Refer to
[vehicle_data.proto](../../interfaces/protobuf/schemas/edgeToCloud/vehicle_data.proto).

### Cloud to Device communication

The Cloud Control plane services publish to FWE dedicated MQTT Topic the following two artifacts:

**Decoder Manifest:** This artifact describes the Vehicle Network Interfaces that the user defined.
The description includes the semantics of each of the Network interface traffic to be inspected
including the signal decoding rules. Refer to
[decoder_manifest.proto](../../interfaces/protobuf/schemas/cloudToEdge/decoder_manifest.proto).

**Collection Scheme:** This artifact describes effectively the inspection rules, that FWE will apply
on the network traffic it receives. Using the decoder manifest, the Inspection module will apply the
rules defined in the collection schemes to generate data snapshots. Refer to
[collection_schemes.proto](../../interfaces/protobuf/schemas/cloudToEdge/collection_schemes.proto).

## Data Persistency

FWE requires a temporary disk location in order to persist and reload the documents it exchanges
with the cloud services. The persistency operates on three types of documents:

- Collection Schemes data: This data set is persisted during shutdown of FWE, and re-loaded upon
  startup.
- Decoder Manifest Data: This data set is persisted during shutdown of FWE, and re-loaded upon
  startup.
- Data Snapshots: This data set is persisted when there is no connectivity in the system. Upon the
  next startup of the application, the data is reloaded and send if there is connectivity.

The persistency module operates on a fixed/configurable maximum partition size. If there is no space
left, the module does not persist the data.

Persisted data is uploaded once on the bootup. Upload will be repeated after interval that is set in
the static configuration under ["staticConfig"]["persistency"]["persistencyUploadRetryIntervalMs"].
If this value is not set, upload will be retried only on the next bootup.

## Logging

FWE defines a Logger interface and implements a standard output logging backend. The interface can
be extended to support other logging backends if needed.

The logger interface defines the following severity levels :

```cpp
/**
 * @brief Severity levels
 */
enum class LogLevel
{
    Trace,
    Info,
    Warning,
    Error,
    Off
};
```

Customers can set the System level logging severity externally via the software configuration file
described below in the configuration section. Each log entry includes the following attributes:

```
[Thread: ID] [Time] [Level] [Filename:LineNumber] [Function()]: [Message]
```

- **Thread:** the thread ID triggering the log entry.
- **Time:** the timestamp in milliseconds since Epoch.
- **Level:** the severity of the log entry.
- **Filename:** the file name that invoked the log entry.
- **LineNumber:** the line in the file that invoked the log entry.
- **Function:** the function name that invoked the log entry.
- **Message:** the actual log message.

## Configuration

| Category                    | Attributes                                  | Description                                                                                                                                                                                                                                                                                                                                                                     | DataType |
| --------------------------- | ------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| canInterface                | interfaceName                               | Interface name for CAN network                                                                                                                                                                                                                                                                                                                                                  | string   |
|                             | protocolName                                | Protocol used- CAN or CAN-FD                                                                                                                                                                                                                                                                                                                                                    | string   |
|                             | protocolVersion                             | Protocol version used- 2.0A, 2.0B.                                                                                                                                                                                                                                                                                                                                              | string   |
|                             | interfaceId                                 | Every CAN signal decoder is associated with a CAN network interface using a unique Id                                                                                                                                                                                                                                                                                           | string   |
|                             | type                                        | Specifies if the interface carries CAN or OBD signals over this channel, this will be CAN for a CAN network interface                                                                                                                                                                                                                                                           | string   |
|                             | timestampType                               | Defines which timestamp type should be used: Software, Hardware or Polling. Default is Software.                                                                                                                                                                                                                                                                                | string   |
| obdInterface                | interfaceName                               | CAN Interface connected to OBD bus                                                                                                                                                                                                                                                                                                                                              | string   |
|                             | obdStandard                                 | OBD Standard (eg. J1979 or Enhanced (for advanced standards))                                                                                                                                                                                                                                                                                                                   | string   |
|                             | pidRequestIntervalSeconds                   | Interval used to schedule PID requests (in seconds)                                                                                                                                                                                                                                                                                                                             | integer  |
|                             | dtcRequestIntervalSeconds                   | Interval used to schedule DTC requests (in seconds)                                                                                                                                                                                                                                                                                                                             | integer  |
|                             | interfaceId                                 | Every OBD signal decoder is associated with a OBD network interface using a unique Id                                                                                                                                                                                                                                                                                           | string   |
|                             | type                                        | Specifies if the interface carries CAN or OBD signals over this channel, this will be OBD for a OBD network interface                                                                                                                                                                                                                                                           | string   |
| bufferSizes                 | dtcBufferSize                               | Max size of the buffer shared between data collection module (Collection Engine) and Vehicle Data Consumer. This is a single producer single consumer buffer.                                                                                                                                                                                                                   | integer  |
|                             | decodedSignalsBufferSize                    | Max size of the buffer shared between data collection module (Collection Engine) and Vehicle Data Consumer for OBD and CAN signals. This buffer receives the raw packets from the Vehicle Data e.g. CAN bus and stores the decoded/filtered data according to the signal decoding information provided in decoder manifest. This is a multiple producer single consumer buffer. | integer  |
|                             | rawCANFrameBufferSize                       | Max size of the buffer shared between Vehicle Data Consumer and data collection module (Collection Engine). This buffer stores raw CAN frames coming in from the CAN Bus. This is a lock-free multi-producer single consumer buffer.                                                                                                                                            | integer  |
| threadIdleTimes             | inspectionThreadIdleTimeMs                  | Sleep time for inspection engine thread if no new data is available (in milliseconds)                                                                                                                                                                                                                                                                                           | integer  |
|                             | socketCANThreadIdleTimeMs                   | Sleep time for CAN interface if no new data is available (in milliseconds)                                                                                                                                                                                                                                                                                                      | integer  |
|                             | canDecoderThreadIdleTimeMs                  | Sleep time for CAN decoder thread if no new data is available (in milliseconds)                                                                                                                                                                                                                                                                                                 | integer  |
| persistency                 | persistencyPath                             | Local storage path to persist Collection Scheme, decoder manifest and data snapshot                                                                                                                                                                                                                                                                                             | string   |
|                             | persistencyPartitionMaxSize                 | Maximum size allocated for persistency (Bytes)                                                                                                                                                                                                                                                                                                                                  | integer  |
|                             | persistencyUploadRetryIntervalMs            | Interval to wait before retrying to upload persisted signal data (in milliseconds). After successfully uploading, the persisted signal data will be cleared. Only signal data that could not be uploaded will be persisted. (in milliseconds)                                                                                                                                   | integer  |
| internalParameters          | readyToPublishDataBufferSize                | Size of the buffer used for storing ready to publish, filtered data                                                                                                                                                                                                                                                                                                             | integer  |
|                             | systemWideLogLevel                          | Sets logging level severity: `Trace`, `Info`, `Warning`, `Error`                                                                                                                                                                                                                                                                                                                | string   |
|                             | logColor                                    | Whether logs should be colored: `Auto`, `Yes`, `No`. Default to `Auto`, meaning FWE will try to detect whether colored output is supported (for example when connected to a tty)                                                                                                                                                                                                | string   |
|                             | maximumAwsSdkHeapMemoryBytes                | The maximum size of AWS SDK heap memory                                                                                                                                                                                                                                                                                                                                         | integer  |
|                             | dataReductionProbabilityDisabled            | Disables probability-based DDC (only for debug purpose)                                                                                                                                                                                                                                                                                                                         | boolean  |
|                             | metricsCyclicPrintIntervalMs                | Sets the interval in milliseconds how often the application metrics should be printed to stdout. Default 0 means never                                                                                                                                                                                                                                                          | string   |
| publishToCloudParameters    | maxPublishMessageCount                      | Maximum messages that can be published to the cloud in one payload                                                                                                                                                                                                                                                                                                              | integer  |
|                             | collectionSchemeManagementCheckinIntervalMs | Time interval between collection schemes checkins(in milliseconds)                                                                                                                                                                                                                                                                                                              | integer  |
| mqttConnection              | endpointUrl                                 | AWS account's IoT device endpoint                                                                                                                                                                                                                                                                                                                                               | string   |
|                             | connectionType                              | The connection module type. It can be `iotCore`, or `iotGreengrassV2` when `FWE_FEATURE_GREENGRASSV2` is enabled.                                                                                                                                                                                                                                                               | string   |
|                             | clientId                                    | The ID that uniquely identifies this device in the AWS Region                                                                                                                                                                                                                                                                                                                   | string   |
|                             | collectionSchemeListTopic                   | Topic for subscribing to Collection Scheme                                                                                                                                                                                                                                                                                                                                      | string   |
|                             | decoderManifestTopic                        | Topic for subscribing to Decoder Manifest                                                                                                                                                                                                                                                                                                                                       | string   |
|                             | canDataTopic                                | Topic for sending collected data to cloud                                                                                                                                                                                                                                                                                                                                       | string   |
|                             | checkinTopic                                | Topic for sending checkins to the cloud                                                                                                                                                                                                                                                                                                                                         | string   |
|                             | certificateFilename                         | The path to the device's certificate file (either `certificateFilename` or `certificate` must be provided)                                                                                                                                                                                                                                                                      | string   |
|                             | privateKeyFilename                          | The path to the device's private key file (either `privateKeyFilename` or `privateKey` must be provided)                                                                                                                                                                                                                                                                        | string   |
|                             | rootCAFilename                              | The path to the root CA certificate file (optional, either `rootCAFilename` or `rootCA` can be provided)                                                                                                                                                                                                                                                                        | string   |
|                             | certificate                                 | The path to the device's certificate file (either `certificateFilename` or `certificate` must be provided)                                                                                                                                                                                                                                                                      | string   |
|                             | privateKey                                  | The path to the device's private key file (either `privateKeyFilename` or `privateKey` must be provided)                                                                                                                                                                                                                                                                        | string   |
|                             | rootCA                                      | The path to the root CA certificate file (optional, either `rootCAFilename` or `rootCA` can be provided)                                                                                                                                                                                                                                                                        | string   |
|                             | metricsUploadTopic                          | Topic used to upload application metrics in plain json. Only used if `remoteProfilerDefaultValues` section is configured                                                                                                                                                                                                                                                        | string   |
|                             | loggingUploadTopic                          | Topic used to upload log messages in plain json. Only used if `remoteProfilerDefaultValues` section is configured                                                                                                                                                                                                                                                               | string   |
| remoteProfilerDefaultValues | loggingUploadLevelThreshold                 | Only log messages with this or higher severity will be uploaded                                                                                                                                                                                                                                                                                                                 | integer  |
|                             | metricsUploadIntervalMs                     | The interval in milliseconds to wait for uploading new values of all metrics                                                                                                                                                                                                                                                                                                    | integer  |
|                             | loggingUploadMaxWaitBeforeUploadMs          | The maximum time in milliseconds to cache log messages before uploading them                                                                                                                                                                                                                                                                                                    | string   |
|                             | profilerPrefix                              | The prefix used to categorize the metrics and logs. Can be set unique per vehicle such as `clientId` or the same for all vehicles if metrics should be aggregated                                                                                                                                                                                                               | string   |

## Security

FWE has been designed with security principles in mind. Security has been incorporated into four
main domains:

- Device Authentication: FWE uses Client Certificates (x.509) to communicate with AWS IoT services.
  All the communications from and to FWE are over a secure TLS Connection. Refer to the
  [AWS IoT Security documentation](https://docs.aws.amazon.com/iot/latest/developerguide/x509-client-certs.html)
  for further details.
- Data in transit: All the data exchanged with the AWS IoT services is encrypted in transit.
- Data at rest: the current version of the software does not encrypt the data at rest i.e. during
  persistency. It's assumed that the software operates in a secure partition that the OEM puts in
  place and rely on the OEM secure storage infrastructure that is applied for all IO operations
  happening in the gateway e.g. via HSM, OEM crypto stack.
- Access to vehicle CAN data: FWE assumes that the software operates in a secure execution
  partition, that guarantees that if needed, the CAN traffic is encrypted/decrypted by the OEM
  Crypto stack (either on chip/HSM or via separate core running the crypto stack).

FWE can be extended to invoke cryptography APIs to encrypt and decrypt the data as per the need.

FWE has been designed to be deployed in a non safety relevant in-vehicle domain/partition. Due to
its use of dynamic memory allocation, this software is not suited for deployment on real time/lock
step/safety cores.

### Best Practices and recommendation

You can use the cmake build option, `FWE_SECURITY_COMPILE_FLAGS`, to enable security-related compile
options when building the binary. Consult the compiler manual for the effect of each option in
`./cmake/compiler_gcc.cmake`. This flag is already enabled in the default
[native compilation script](./tools/build-fwe-native.sh) and
[cross compilation script for ARM64](./tools/build-fwe-cross-arm64.sh)

Customers are encouraged to store key materials on hardware modules, such as hardware security
module (HSM), Trusted Platform Modules (TPM), or other cryptographic elements. A HSM is a removable
or external device that can generate, store, and manage RSA keys used in asymmetric encryption. A
TPM is a cryptographic processor present on most commercial PCs and servers.

Please refer to
[AWS IoT Security Best Practices](https://docs.aws.amazon.com/iot/latest/developerguide/security-best-practices.html)
for recommended security best practices.

Please refer to
[Device Manufacturing and Provisioning with X.509 Certificates in AWS IoT Core](https://d1.awsstatic.com/whitepapers/device-manufacturing-provisioning.pdf)
for security recommendations on device manufacturing and provisioning.

Note: This is **only** a recommendation. You are responsible for protecting your system with proper
security measures.

## Supported Platforms

FWE has been developed for 64 bit architecture. It has been tested on both ARM and x86 multicore
based machines, with a Linux Kernel version of 5.4 and above. The kernel module for ISO-TP
(`can-isotp`) would need to be installed in addition for kernels below 5.10.

## Getting Help

Contact [AWS Support](https://aws.amazon.com/contact-us/) if you have any technical questions about
FWE.

## Resources

The following documents or websites provide more information about FWE.

1. [Change Log](../../CHANGELOG.md) provides a summary of feature enhancements, updates, and
   resolved and known issues.
1. [Offboarding and Data Deletion](../AWS-IoTFleetWiseOffboarding.md) provides a summary of the
   steps needed on the client side to offboard from the service.

# Tutorial: Run AWS IoT FleetWise Edge Agent on a Raspberry Pi

**Copyright © Amazon Web Services, Inc. and/or its affiliates. All rights reserved.**

Amazon's trademarks and trade dress may not be used in connection with any product or service that
is not Amazon's, in any manner that is likely to cause confusion among customers, or in any manner
that disparages or discredits Amazon. All other trademarks not owned by Amazon are the property of
their respective owners, who may or may not be affiliated with, connected to, or sponsored by
Amazon.

**Note**

* AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe (Frankfurt).
* The AWS IoT FleetWise in-vehicle software component is licensed to you under the Amazon Software
  License. You are solely responsible for ensuring such software and any updates and modifications
  thereto are deployed and maintained safely and securely in any vehicles and do not otherwise
  impact vehicle safety.

## Topics

* [Prerequisites](#prerequisites)
* [Instructions](#instructions)

## Prerequisites

* A Raspberry Pi, version 3 or later, (64-bit) 
* An SD-card, with a minimum of 4 GB storage
* A CAN ‘Hat’ for Raspberry Pi with an MCP2515 CAN controller such as the
  [XYGStudy 2-Channel Isolated CAN Bus Expansion HAT](https://www.amazon.com/Raspberry-2-Channel-SN65HVD230-Protection-XYGStudy/dp/B087PWBFV8?th=1),
  [Coolwell Waveshare 2-Channel Isolated CAN Bus Expansion Hat](https://www.amazon.de/-/en/Waveshare-CAN-HAT-SN65HVD230-Protection/dp/B087PWNMM8/?th=1),
  or the [2-Channel Isolated CAN Bus Expansion HAT](https://rarecomponents.com/store/2-ch-can-hat-waveshare).

## Instructions

1. Download Ubuntu 20.04 for RPi 64-bit (https://ubuntu.com/download/raspberry-pi) on a local
   Windows, Mac, or Linux machine.
2. To flash (write operating system image) to the SD card, use [Balena Etcher](https://www.balena.io/etcher/)
   (available for Windows, Mac and Linux).
3. Insert the SD card into your Raspberry Pi, attach the CAN hat, connect the Raspberry Pi to your
   internet router, and turn on the power.
4. SSH to Raspberry Pi, using the initial password `ubuntu`: (Note: If connecting to the hostname
   `ubuntu` doesn’t work, find the IP address from your internet router instead.)
```
ssh ubuntu@ubuntu
```
7. Run the following to update the system and install `unzip`:
```
sudo apt update
sudo apt upgrade -y
sudo apt install -y unzip
```
8. Run `sudo nano /boot/firmware/usercfg.txt` and add the following lines to enable the CAN hat:
```
dtparam=spi=on
dtoverlay=mcp2515-can1,oscillator=16000000,interrupt=25
dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=23
dtoverlay=spi-bcm2835-overlay
```
9. Save the file (`CTRL+O`, `CTRL+X`) and reboot the Raspberry Pi (`sudo reboot`).
10. Deploy AWS IoT Fleetwise Edge Agent to the board, as described in the NXP S32G section of the
    [AWS IoT FleetWise Edge Agent Developer Guide](../dev-guide/edge-agent-dev-guide.md#provision-aws-iot-credentials).
11. Install the [can-isotp](https://en.wikipedia.org/wiki/ISO_15765-2) module:
```
sudo ~/aws-iot-fleetwise-edge/tools/install-socketcan.sh
```
12. Run `sudo nano /usr/local/bin/setup-socketcan.sh` and add the following lines to bring up the
    `can0` and `can1` interfaces at startup:
```
ip link set up can0 txqueuelen 1000 type can bitrate 500000 restart-ms 100
ip link set up can1 txqueuelen 1000 type can bitrate 500000 restart-ms 100
```
13. Restart the setup-socketcan service and the IoT FleetWise Edge Agent service:
```
sudo systemctl restart setup-socketcan
sudo systemctl restart fwe@0
```
14. To verify the IoT FleetWise Edge Agent is running and is connected to the cloud, check the Edge
    Agent log files:
``` 
sudo journalctl -fu fwe@0
```
- Look for this message to verify:
```
[INFO] [AwsIotConnectivityModule::connect]: [Connection completed successfully.]
```
- Use the [troubleshooting information and solutions](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/troubleshooting.html)
  in the AWS IoT FleetWise Developer Guide to help resolve issues with AWS IoT FleetWise Edge Agent.

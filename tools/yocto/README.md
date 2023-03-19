# Yocto Reference Project for AWS IoTFleetWise

The project is based on NXP Linux BSP 35.0 for the S32G-VNP-RDB2 board which can be downloaded from:
https://github.com/nxp-auto-linux/auto_yocto_bsp

This folder provides an additional meta layer on top of BSP 35.0, in order to add the following:

- `linux-s32` - Adds the CAN ISO-TP kernel module
- `can-utils` - Adds a more recent version of this package
- `setup-socketcan` - Adds a `systemd` service to bring up SocketCAN interfaces at startup

## Prerequisites

Run the following script to install the prerequisites on an Ubuntu 20.04 host machine (assuming you
have unzipped `aws-iot-fleetwise-edge.zip` to `~/aws-iot-fleetwise-edge`):

    sudo ~/aws-iot-fleetwise-edge/tools/install-deps-yocto.sh

## Setup Yocto Project

Run the following script to clone the NXP Linux BSP 35.0 Yocto project for the S32G and add the
`meta-aws-iot-fleetwise` layer:

    mkdir -p ~/yocto-build && cd ~/yocto-build
    ~/aws-iot-fleetwise-edge/tools/setup-yocto-s32g.sh

## Build SD-Card Image

Then run `bitbake` as follows to build the Yocto project and create the SD-card image:

    source sources/poky/oe-init-build-env build_s32g274ardb2ubuntu
    bitbake fsl-image-ubuntu

The resulting SD card image can then be found here:
`build_s32g274ardb2ubuntu/tmp/deploy/images/s32g274ardb2/fsl-image-ubuntu-s32g274ardb2.sdcard`

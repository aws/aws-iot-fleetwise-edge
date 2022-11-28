# Tutorial: Run AWS IoT FleetWise Edge Agent on iWave G26 TCU

**Copyright © Amazon Web Services, Inc. and/or its affiliates. All rights reserved.**

Amazon's trademarks and trade dress may not be used in connection with any product or service that
is not Amazon's, in any manner that is likely to cause confusion among customers, or in any manner
that disparages or discredits Amazon. All other trademarks not owned by Amazon are the property of
their respective owners, who may or may not be affiliated with, connected to, or sponsored by
Amazon.

**Note**

* AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe (Frankfurt).
* The AWS IoT FleetWise in-vehicle software component is licensed to you under the Apache License, Version 2.0.
* You are solely responsible for ensuring such software and any updates and modifications
  thereto are deployed and maintained safely and securely in any vehicles and do not otherwise
  impact vehicle safety.

## About this tutorial

AWS IoT FleetWise provides a set of tools for automakers to collect, transform, and
transfer vehicle data to the cloud at scale. With AWS IoT FleetWise, you can build virtual
representations of vehicle networks and define data collection rules to transfer only high-value
data from your vehicles to the AWS cloud. Follow the steps in this tutorial to set up and configure the
iWave G26 TCU device hardware to work with the AWS IoT FleetWise vehicle agent software. You can then connect the device to a vehicle so that it collects vehicle J1979 OBD-II data and transfers it to the AWS IoT FleetWise service.
To additionally also collected GPS data more steps described here [iwavegps](../../src/datamanagement/custom/example/iwavegps/README.md) are required. They require the steps below to be executed first.

**Estimated Time**: 60 minutes

## Topics

* [Prerequisites](#prerequisites)
* [Step 1: Set up the iWave Systems G26 TCU](#step-1-set-up-iwave-systems-g26-tcu)
* [Step 2: Launch your development machine](#step-2-launch-your-development-machine)
* [Step 3: Compile AWS IoT FleetWise Edge Agent software](#step-3-compile-aws-iot-fleetwise-edge-agent-software)
* [Step 4: Provision AWS IoT credentials](#step-4-provision-aws-iot-credentials)
* [Step 5: Deploy Edge Agent](#step-5-deploy-edge-agent)
* [Step 6: Connect the iWave G26 TCU to the vehicle](#step-6-connect-the-tcu-to-the-vehicle)
* [Step 7: Collect OBD Data](#step-7-collect-obd-data)

## Prerequisites

* A [G26 TCU from iWave Systems](https://www.iwavesystems.com/product/telematics-control-unit/) device
* Access to an AWS account with administrator permissions
* To be signed in to the AWS Management Console with an account in your chosen Region
  * Note: AWS IoT FleetWise is currently available in US East (N. Virginia) and Europe (Frankfurt).
* A SIM card
* A local Linux or MacOS machine

## Step 1: Set up iWave Systems G26 TCU
### To set up iWave Systems G26 TCU

1. Unpack and set up the iWave G26 TCU. For instructions on setting up the iWave G26 TCU, see *Section 2* of the [Telematics Control Unit Product User Manual](https://www.iwavesystems.com/wp-content/uploads/2021/07/Telematics-Solution-User-Manual_iW-PRGET-UM-01-R3.0-REL1.4.pdf).
1. Install the SIM card in the device and connect the device to the power supply. The device boots up.
1. Join the device's Wi-Fi hotspot from your local machine by using the SSID and password. For more information, see *Section 3.5* of the [Telematics Control Unit Product User Manual](https://www.iwavesystems.com/wp-content/uploads/2021/07/Telematics-Solution-User-Manual_iW-PRGET-UM-01-R3.0-REL1.4.pdf). If the device has a problem connecting, wait another minute and try again. For security reasons, we recommend you change this password in `/etc/hostapd.conf` - search for `wpa_passphrase`.
1. On your local machine, to find the IP address that is used for the Wi-Fi interface, run `ifconfig`.
The TCU device has the same IP address except the last digit is `1`. For example,
if the IP address of your local machine is `192.168.43.20`, the IP address of the TCU is
`192.168.43.1`.
1. On your local machine, connect to the TCU through SSH. The default password is in
*Section 3.5* of the [Telematics Control Unit Product User Manual](https://www.iwavesystems.com/wp-content/uploads/2021/07/Telematics-Solution-User-Manual_iW-PRGET-UM-01-R3.0-REL1.4.pdf). For security reasons, we recommend you change this password by running the `passwd` command:

   ```bash
   ssh root@<TCU_IP_ADDRESS>
   ```

### To set up CAN 

1. Confirm the TCU firmware has the required Linux kernel module by running the following command:

   ```bash
   lsmod | grep can_isotp
   ```
   A line starting with `can_isotp` is returned. If nothing is returned, contact iWave Systems [technical support](https://www.iwavesystems.com/contact-us/) and ask for an updated firmware image that     contains the `can-isotp` kernel module.

1. To open the CAN interfaces at startup, first create the script `/usr/bin/setup-socketcan.sh`
with the following content:

   ```bash
   #!/bin/sh
   ip link set up can0 txqueuelen 1000 type can bitrate 500000 restart-ms 100
   ip link set up can1 txqueuelen 1000 type can bitrate 500000 restart-ms 100
   ```

1. To make the script executable, run the following command:

   ```bash
   chmod +x /usr/bin/setup-socketcan.sh.
   ```

1. To run the script at startup, create the file `/lib/systemd/system/setup-socketcan.service` with the following content:

   ```ini
   [Unit]
   Description=Setup SocketCAN interfaces
   After=multi-user.target
   [Service]
   Type=oneshot
   RemainAfterExit=yes
   ExecStart=/usr/bin/setup-socketcan.sh
   [Install]
   WantedBy=multi-user.target
   ```

1. To start and enable the service, run the following:

   ```bash
   systemctl start setup-socketcan
   systemctl enable setup-socketcan
   ```

### To set up the modem

Setting up the 4G LTE modem inside the G26 TCU is dependent on your SIM card service provider.

1. Edit the file `/etc/ppp/chat/gprs`, replacing the content with the following template:

   ```
   TIMEOUT 35
   ECHO ON
   ABORT '\nBUSY\r'
   ABORT '\nERROR\r'
   ABORT '\nNO ANSWER\r'
   ABORT '\nNO CARRIER\r'
   ABORT '\nNO DIALTONE\r'
   ABORT '\nRINGING\r\n\r\nRINGING\r'
   '' \rAT

   # If your SIM requires a PIN, uncomment and change 0000 to your PIN:
   #OK AT+CPIN=0000

   OK 'ATQ0 V1 E1 S0=0 &C1 &D2'

   # Replace <APN> with the APN for your service provider:
   OK AT+CGDCONT=1,"IP","<APN>"

   # If dialing fails, try the alternate command below by commenting out the first command and
   # uncommenting the second:
   OK ATD*99***1#
   #OK ATD*99#

   CONNECT ''
   ```

   Edit the file as follows:
   1. Configure the access point name (APN) setting for your service provider:
      1. For Verizon in the US, replace `<APN>` with `vzwinternet`.
      1. For ALDI Talk in Germany, replace `<APN>` with `internet.eplus.de`.
      1. Some other service providers allow the default APN to be used. To try this, replace the entire
      line with `OK AT+CGDCONT=1,"IP"`.
   1. If your SIM card has a PIN number, remove the comment character `#` from the line containing `OK AT+CPIN=0000` and change
   `0000` to your PIN number.
   1. If dialing fails, try the alternate command as described in the template.

1. Edit the file `/etc/ppp/peers/gprs_4g`, replacing the content with the following template:

   ```
   file /etc/ppp/options-mobile

   # If your service provider uses authentication for the APN, uncomment the following lines and
   # replace <APN_USERNAME> and <APN_PASSWORD> with the required authentication values:
   #user "<APN_USERNAME>"
   #password "<APN_PASSWORD>"

   connect "/usr/sbin/chat -v -t15 -f /etc/ppp/chat/gprs"
   ```

   If your service provider uses authentication for the APN, remove the comment character `#` from the `user` and `password` lines, and replace `<APN_USERNAME>` and `<APN_PASSWORD>` with your authentication settings.
   
   For ALDI Talk in Germany, replace `<APN_USERNAME>` with `eplus` and `<APN_PASSWORD>` with `gprs`.

1. To turn on the modem and connect it to the mobile network, create the script `/usr/bin/start_lte.sh` and add the following content:

   ```bash
   #!/bin/sh
   echo 1 > /proc/sys/net/ipv4/ip_forward
   #12v register
   echo 137 > /sys/class/gpio/export
   #Battery Status
   echo 118 > /sys/class/gpio/export
   #Battery Charge Enable
   echo 120 > /sys/class/gpio/export
   #LED
   echo 73 > /sys/class/gpio/export
   #Battery Power Good
   echo 64 > /sys/class/gpio/export
   #Modem
   echo 90 > /sys/class/gpio/export
   echo 78 > /sys/class/gpio/export
   echo 88 > /sys/class/gpio/export
   echo out > /sys/class/gpio/gpio137/direction
   echo out > /sys/class/gpio/gpio120/direction
   echo out > /sys/class/gpio/gpio73/direction
   echo in > /sys/class/gpio/gpio118/direction
   echo in > /sys/class/gpio/gpio64/direction
   echo out > /sys/class/gpio/gpio90/direction
   echo out > /sys/class/gpio/gpio78/direction
   echo out > /sys/class/gpio/gpio88/direction
   sleep 1
   echo 0 > /sys/class/gpio/gpio137/value
   echo 1 > /sys/class/gpio/gpio90/value
   echo 1 > /sys/class/gpio/gpio78/value
   sleep 1
   echo 0 > /sys/class/gpio/gpio88/value
   sleep 1
   echo 1 > /sys/class/gpio/gpio88/value
   sleep 1
   echo 0 > /sys/class/gpio/gpio88/value
   sleep 1
   insmod /iwtest/kernel-module/udc-core.ko
   insmod /iwtest/kernel-module/libcomposite.ko
   insmod /iwtest/kernel-module/ci_hdrc.ko
   insmod /iwtest/kernel-module/usbmisc_imx.ko
   insmod /iwtest/kernel-module/ci_hdrc_imx.ko
   insmod /iwtest/kernel-module/u_serial.ko
   sleep 20
   /usr/sbin/pppd call gprs_4g nodetach
   ```

   To make the script executable, run `chmod +x /usr/bin/start_lte.sh`.

   Run the `/usr/bin/start_lte.sh` script. A red LED on the TCU board lights up.
After 30 seconds, if connection to the internet is successful, you see the following in
the output:

   ```
   Script /usr/sbin/chat -v -t15 -f /etc/ppp/chat/gprs finished (pid XXX), status = 0x0
   Serial connection established.
   ...
   Script /etc/ppp/ip-up started (pid XXX)
   Script /etc/ppp/ip-up finished (pid XXX), status = 0x0
   ```

1. Confirm the internet connection is working by opening another SSH connection to the TCU and
running `ping amazon.com`. If the connection is working, press CTRL+C to stop the script in the first
terminal.

   If you have trouble connecting to the internet, try taking out the SIM card and
checking that it works in a smartphone. Otherwise, contact iWave Systems [technical support](https://www.iwavesystems.com/contact-us/) for help. 

1. To connect to the internet at startup, create the file `/lib/systemd/system/lte.service`
with the following contents:

   ```ini
   [Unit]
   Description=LTE Service
   Before=network.target
   [Service]
   ExecStart=/usr/bin/start_lte.sh
   [Install]
   WantedBy=multi-user.target
   ```

1. To start and enable the service, run the following command:

   ```bash
   systemctl enable lte
   systemctl start lte
   ```

## Step 2: Launch your development machine

These steps require an Ubuntu 18.04 development machine with 10 GB free disk space. If necessary, you can use a local Intel x86_64 (amd64) machine. We recommended using the following instructions to
launch an AWS EC2 Graviton (arm64) instance. For more information about Amazon EC2 pricing, see [Amazon EC2 On-Demand Pricing](https://aws.amazon.com/ec2/pricing/on-demand/).

### To launch an Amazon EC2 instance with administrator permissions

1. Sign in to your [AWS account](https://aws.amazon.com/console/).
1. Open the [**Launch CloudFormation Template**](https://us-east-1.console.aws.amazon.com/cloudformation/home?region=us-east-1#/stacks/quickcreate?templateUrl=https%3A%2F%2Faws-iot-fleetwise.s3.us-west-2.amazonaws.com%2Flatest%2Fcfn-templates%2Ffwdev.yml&stackName=fwdev&param_Ec2VolumeSize=20).
1. Enter the **Name** of an existing SSH key pair in your account from [here](https://us-east-1.console.aws.amazon.com/ec2/v2/home?region=us-east-1#KeyPairs:).
   * Don't include the file suffix `.pem`.
   * If you don't have an SSH key pair, [create one](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/create-key-pairs.html) and download the corresponding `.pem` file. Be sure to update the file permissions: `chmod 400 <PATH_TO_PEM>`
1. Select **I acknowledge that AWS CloudFormation might create IAM resources with custom names.**
1. Choose **Create stack**. Wait until the status of the Stack is **CREATE_COMPLETE**. This can take up to five minutes.
1. Choose the **Outputs** tab, copy the EC2 IP address, and connect from your local machine through SSH to the development machine.

   ```bash
   ssh -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>
   ```

## Step 3: Compile AWS IoT FleetWise Edge Agent software

Next, compile the AWS IoT FleetWise Edge Agent software for the ARM 32-bit architecture of the
i.MX6 processor present in the G26 TCU device.

### To compile the AWS IoT FleetWise Edge Agent software

1. On your development machine, clone the latest AWS IoT FleetWise Edge Agent
   software from GitHub by running the following:

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
     && cd ~/aws-iot-fleetwise-edge
   ```

2. Install the AWS IoT FleetWise Edge Agent dependencies. The command below installs the following Ubuntu packages for cross-compiling the Edge Agent for ARM 32-bit:

 `libssl-dev libboost-system-dev libboost-log-dev libboost-thread-dev build-essential cmake unzip git wget curl zlib1g-dev libcurl4-openssl-dev libsnappy-dev default-jre libasio-dev`.

   Additionally, it installs the following: `jsoncpp protobuf aws-sdk-cpp`.

   ```bash
   sudo -H ./tools/install-deps-cross-armhf.sh
   ```

3. To compile AWS IoT FleetWise Edge Agent software, run the following command:

   ```bash
   ./tools/build-fwe-cross-armhf.sh
   ```

## Step 4: Provision AWS IoT credentials

On the development machine, create an IoT thing and provision its credentials by running the following command. The AWS IoT FleetWise Edge Agent binary and its configuration files are packaged into a ZIP file that is ready for deployment to the TCU.

   ```bash
   mkdir -p ~/aws-iot-fleetwise-deploy && cd ~/aws-iot-fleetwise-deploy \
     && cp -r ~/aws-iot-fleetwise-edge/tools . \
     && mkdir -p build/src/executionmanagement \
     && cp ~/aws-iot-fleetwise-edge/build/src/executionmanagement/aws-iot-fleetwise-edge \
       build/src/executionmanagement/ \
     && mkdir -p config && cd config \
     && ../tools/provision.sh \
       --vehicle-name fwdemo-g26 \
       --certificate-pem-outfile certificate.pem \
       --private-key-outfile private-key.key \
       --endpoint-url-outfile endpoint.txt \
       --vehicle-name-outfile vehicle-name.txt \
     && ../tools/configure-fwe.sh \
       --input-config-file ~/aws-iot-fleetwise-edge/configuration/static-config.json \
       --output-config-file config-0.json \
       --vehicle-name `cat vehicle-name.txt` \
       --endpoint-url `cat endpoint.txt` \
       --can-bus0 can0 \
     && cd .. && zip -r aws-iot-fleetwise-deploy.zip .
   ```

## Step 5: Deploy Edge Agent

### To deploy AWS IoT FleetWise Edge Agent

1. On your local machine, copy the deployment ZIP file from the machine with Amazon EC2 to your local machine by running the following command:

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:aws-iot-fleetwise-deploy/aws-iot-fleetwise-deploy.zip .
   ```

1. On your local machine, copy the deployment ZIP file from your local machine to the TCU by running the following command:

   ```bash
   scp aws-iot-fleetwise-deploy.zip root@<TCU_IP_ADDRESS>:
   ```

1. As described in step 5 of [setting up the TCU](#step-1-set-up-iwave-systems-g26-tcu), connect through SSH to the TCU. On the TCU, install AWS IoT FleetWise Edge Agent as a service by running the following command:

   ```bash
   mkdir -p ~/aws-iot-fleetwise-deploy && cd ~/aws-iot-fleetwise-deploy \
     && unzip -o ~/aws-iot-fleetwise-deploy.zip \
     && mkdir -p /etc/aws-iot-fleetwise \
     && cp config/* /etc/aws-iot-fleetwise \
     && ./tools/install-fwe.sh
   ```

1. On the TCU, view and follow the AWS IoT FleetWise Edge Agent log (press CTRL+C to exit) by running the following command:

   ```bash
   journalctl -fu fwe@0
   ```

   The following line appears, confirming the AWS IoT FleetWise Edge Agent successfully connected to AWS IoT Core:

   ```
   [INFO] [AwsIotConnectivityModule::connect]: [Connection completed successfully.]
   ```

## Step 6: Connect the TCU to the vehicle

1. Connect the On-Board Diagnostic (OBD) connector for the TCU to the OBD port on your vehicle. The OBD port can often be found under the dashboard on the driver's side of the car. For example locations of the OBD port, see the following photos. 

**Note**
* If you can't find the OBD port on your vehicle, try searching [YouTube](https://www.youtube.com/) for your vehicle make, model, and OBD port location.

![Alt](obd-port-1.jpg "OBD port location under fuel door release button")
![Alt](obd-port-2.jpg "OBD port location under vehicle dashboard")

2. After the OBD is connected, start the engine. 

## Step 7: Collect OBD data

1. On the development machine, install the AWS IoT FleetWise cloud demo script dependencies by running the following commands.
   The script installs the following Ubuntu packages: `python3.7 python3-setuptools curl`, and then installs Python PIP for Python 3.7
   and the following PIP packages: `wrapt plotly pandas cantools`.

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
     && sudo -H ./install-deps.sh
   ```

1. On the development machine, deploy a heartbeat campaign that periodically collects OBD data by running the following commands:

   ```bash
   ./demo.sh --vehicle-name fwdemo-g26 --campaign-file campaign-obd-heartbeat.json
   ```

   The demo script does the following:
      1. Registers your AWS account with AWS IoT FleetWise, if it's not already registered.
      1. Creates a signal catalog. First, the demo script adds standard OBD signals based on `obd-nodes.json`. Next, it adds CAN signals in a flat signal list based on the DBC file `hscan.dbc`.
      1. Creates a vehicle model, or *model manifest*, that references the signal catalog with every
         OBD and DBC signal.
      1. Activates the vehicle model.
      1. Creates a decoder manifest linked to the vehicle model using `obd-decoders.json` for
         decoding OBD signals from the network interfaces defined in `network-interfaces.json`.
      1. Imports the CAN signal decoding information from `hscan.dbc` to the decoder manifest.
      1. Updates the decoder manifest to set the status as `ACTIVE`.
      1. Creates a vehicle with an ID equal to `fwdemo-g26`, which is also the name passed to
         `provision.sh`.
      1. Creates a fleet.
      1. Associates the vehicle with the fleet.
      1. Creates a campaign from `campaign-obd-heartbeat.json`. This contains a time-based
         collection scheme that collects OBD data and targets the campaign at the fleet.
      1. Approves the campaign.
      1. Waits until the campaign status is `HEALTHY`, which means the campaign was deployed to
         the fleet.
      1. Waits 30 seconds and then downloads the collected data from Amazon Timestream.
      1. Saves the data to an HTML file.

   When the script completes, you receive the path to the output HTML file on your local machine. To download it, use `scp`, and then open it in your web browser:

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:<PATH_TO_HTML_FILE> .
   ```

1. To explore the collected data, click and drag on the graph to zoom in. Alternatively, if your AWS account is enrolled with QuickSight or Amazon Managed Grafana, you can use them to browse the data from Amazon Timestream directly.

**Note:**
* After the vehicle is turned off, the iWave device will stay on for certain period before it goes to 
sleep. The duration depends on when the ECUs stop sending CAN messages, which varies across different
vehicle models. For this reason, if you're not going to turn on the vehicle for an extended period (like a week), unplug the iWave device from the J1962 DCL port to avoid depleting the battery.

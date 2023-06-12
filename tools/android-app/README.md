# Android App for AWS IoT FleetWise Edge

This app demonstrates AWS IoT FleetWise using a smart phone with Android 8.0+ and a commonly
available [ELM327 Bluetooth OBD adapter](https://www.amazon.com/s?k=elm327+bluetooth).

## User guide

1. Download the latest APK from GitHub to the phone:
   https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge.apk

1. Install the app, which will require enabling installation from unknown sources.

1. Open the app, which will ask permission to collect GPS location data and access Bluetooth
   devices.

1. Open the AWS CloudShell: [Launch CloudShell](https://console.aws.amazon.com/cloudshell/home)

1. Run the following script to provision credentials for the app to connect to your AWS account. You
   will be asked to enter the name of an existing S3 bucket, in which the credentials will be saved.
   **Note:** It is important that the chosen S3 bucket is not public.

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
   && cd ~/aws-iot-fleetwise-edge/tools/android-app/cloud \
   && pip3 install segno \
   && ./provision.sh
   ```

1. When the script completes, the path to a QR code image file is given:
   `/home/cloudshell-user/aws-iot-fleetwise-edge/tools/android-app/cloud/config/provisioning-qr-code.png`.
   Copy this path, then click on the Actions drop down menu in the top-right corner of the
   CloudShell window and choose **Download file**. Paste the path to the file, choose **Download**,
   and open the downloaded image.

1. Scan the QR code with the phone, which will open the link in the AWS IoT FleetWise Edge app and
   download the credentials. This will cause the app to connect to the cloud and you should shortly
   see the status `MQTT connection: CONNECTED`.

   - **If scanning the QR code does not open the app, and instead opens a webpage:** Copy the
     webpage link, then open the AWS IoT FleetWise Edge app, go to 'Vehicle configuration' and paste
     the link.

1. **Optional:** If you want to delete the credentials from the S3 bucket for security, run the
   command suggested by the output of the script which will be similar to:

   ```bash
   aws s3 rm s3://<S3_BUCKET_NAME>/fwdemo-android-<TIMESTAMP>-creds.json
   ```

1. In the cloud shell, run the following command to create an AWS IoT FleetWise campaign to collect
   GPS and OBD PID data and send it to Amazon Timestream. After a short time you should see that the
   status in the app is updated with:
   `Campaign ARNs: arn:aws:iotfleetwise:us-east-1:XXXXXXXXXXXX:campaign/fwdemo-android-XXXXXXXXXX-campaign`.

   ```bash
   ./setup-iotfleetwise.sh
   ```

1. Plug in the ELM327 OBD Bluetooth adapter to your vehicle and switch on the ignition.

1. Go to the Bluetooth menu of your phone, then pair the ELM327 Bluetooth adapter. Typically the the
   name of the device is `OBDII` and the pairing PIN number is `1234`.

1. Go back to the AWS IoT FleetWise Edge app, then go to 'Bluetooth device' and select the ELM327
   device you just paired.

1. After a short time you should see that the status is updated with:
   `Bluetooth: Connected to ELM327 vX.X` and `Supported OBD PIDs: XX XX XX`.

1. Go to the [Amazon Timestream console](https://us-east-1.console.aws.amazon.com/timestream/home)
   and view the collected data.

1. **Optional:** If you want to clean up the resources created by the `provision.sh` and
   `setup-iotfleetwise.sh` scripts, run the following command. **Note:** this will not delete the
   Amazon Timestream database.

   ```bash
   ./clean-up.sh
   ```

## Developer guide

This guide details how to build the app from source code and use the shared library in your own
Android apps.

### Build guide

An Ubuntu 20.04 development machine with 200GB free disk space should be used.

1. Clone the source code:

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
   && cd ~/aws-iot-fleetwise-edge
   ```

1. Install the dependencies:

   ```bash
   sudo -H ./tools/install-deps-cross-android.sh
   ```

1. Build the shared libraries:

   ```bash
   ./tools/build-fwe-cross-android.sh \
   && ./tools/build-dist.sh \
       build/arm64-v8a/src/executionmanagement/libaws-iot-fleetwise-edge.so:arm64-v8a \
       build/armeabi-v7a/src/executionmanagement/libaws-iot-fleetwise-edge.so:armeabi-v7a
   ```

1. Build the app:

   ```bash
    mkdir -p tools/android-app/app/src/main/jniLibs \
    && cp -r build/dist/arm64-v8a build/dist/armeabi-v7a tools/android-app/app/src/main/jniLibs \
    && cp THIRD-PARTY-LICENSES tools/android-app/app/src/main/assets \
    && cd tools/android-app \
    && export ANDROID_HOME=/usr/local/android_sdk ./gradlew assemble
   ```

### Shared library interface

The C++ code for AWS IoT FleetWise Edge is compiled into a shared library using the Android NDK. The
interface for shared library can be found in the JNI wrapper class
`app/src/main/java/com/aws/iotfleetwise/Fwe.java`. The shared library can also be used in your app
using this interface, which includes a method to ingest raw CAN frame data.

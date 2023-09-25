# Android App for AWS IoT FleetWise

This app demonstrates AWS IoT FleetWise using an Android smartphone or Android Automotive.

- [Android Smartphone User guide](#android-smartphone-user-guide)
- [Android Automotive User Guide](#android-automotive-user-guide)
- [Android Developer Guide](#android-developer-guide)

## Android Smartphone User guide

This guide demonstrates AWS IoT FleetWise using a smartphone with Android 5.0+ and a commonly
available [ELM327 Bluetooth OBD adapter](https://www.amazon.com/s?k=elm327+bluetooth).

1. Download the latest APK from GitHub to the phone:
   https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge.apk

1. Install the app, which will require enabling installation from unknown sources.

1. Open the app, which will ask permission to collect GPS location data and access Bluetooth
   devices.

1. Open the AWS CloudShell: [Launch CloudShell](https://console.aws.amazon.com/cloudshell/home)

1. Run the following command to clone the FWE repo from GitHub:

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge
   ```

1. Run the following script to provision credentials for the app to connect to your AWS account. You
   will be asked to enter the name of an existing S3 bucket, in which the credentials will be saved.
   **Note:** It is important that the chosen S3 bucket is not public.

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/android-app/cloud \
   && pip3 install segno \
   && ./provision.sh --s3-qr-code
   ```

1. When the script completes, the path to a QR code image file is given:
   `/home/cloudshell-user/aws-iot-fleetwise-edge/tools/android-app/cloud/config/provisioning-qr-code.png`.
   Copy this path, then click on the Actions drop down menu in the top-right corner of the
   CloudShell window and choose **Download file**. Paste the path to the file, choose **Download**,
   and open the downloaded image.

1. Scan the QR code with the phone, which will open the link in the app and download the
   credentials. This will cause the app to connect to the cloud and you should shortly see the
   status `MQTT connection: CONNECTED`.

   - **If scanning the QR code does not open the app, and instead opens a webpage:** Copy the
     webpage link, then open the app, go to 'Vehicle configuration' and paste the link.

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

1. Go back to the app, then go to 'Bluetooth device' and select the ELM327 device you just paired.

1. After a short time you should see that the status is updated with:
   `Bluetooth: Connected to ELM327 vX.X` and `Supported OBD PIDs: XX XX XX`.

1. Go to the
   [Amazon Timestream Query editor](https://us-east-1.console.aws.amazon.com/timestream/home?region=us-east-1#query-editor:)
   and run the query suggested by the `setup-iotfleetwise.sh` script, which will be of the form:

   ```
   SELECT * FROM "IoTFleetWiseDB-<TIMESTAMP>"."VehicleDataTable" WHERE vehicleName='fwdemo-android-<TIMESTAMP>' ORDER BY time DESC LIMIT 1000
   ```

1. **Optional:** If you want to clean up the resources created by the `provision.sh` and
   `setup-iotfleetwise.sh` scripts, run the following command. **Note:** this will not delete the
   Amazon Timestream database.

   ```bash
   ./clean-up.sh
   ```

## Android Automotive User Guide

This guide demonstrates AWS IoT FleetWise using the Android emulator running
[Android Automotive (AAOS)](https://source.android.com/docs/automotive/start/what_automotive).
**Note:** this guide uses the pre-built image for AAOS, which means the app will not have access to
privileged VHAL properties. To demonstrate the app accessing privileged VHAL properties, follow the
[Android Automotive system image build guide](#android-automotive-system-image-build-guide).

**Prerequisites:**

- A local x86_64 Ubuntu 20.04 machine with the
  [AWS CLI](https://docs.aws.amazon.com/cli/latest/userguide/getting-started-install.html)
  installed.

### Android emulator setup and app installation

1. Install the preview version of Android Studio: https://developer.android.com/studio/preview

1. From the 'Welcome to Android Studio' window (close any open project), click on `...` -> SDK
   Manager. Under 'SDK Platforms', select 'Show Package Details'. Under 'Android 12L ("Sv2")',
   select 'Automotive with Play Store Intel x86_64 Atom System Image'. Under 'SDK Tools', select
   'Android SDK Platform-Tools'. Click Apply, to download and install the packages.

1. From the 'Welcome to Android Studio' window, click on `...` -> Virtual Device Manager. Under
   Virtual, select 'Create Device'. Under category, select Automotive, then Next, leave the
   recommended release selected, then Next and Finish.

1. Click the refresh button, then the play button to start the emulator. After some time the AAOS
   home screen is shown.

1. Run the following on your local machine to download and install the app:

   ```bash
   wget https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge.apk \
   && ~/Android/Sdk/platform-tools/adb install aws-iot-fleetwise-edge.apk
   ```

### Provision credentials and collect data

1. Run the following script to provision credentials for the app to connect to your AWS account.

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
   && cd ~/aws-iot-fleetwise-edge/tools/android-app/cloud \
   && ./provision.sh
   ```

1. Run the following to configure the app with the credentials. This will cause the app to connect
   to the cloud and you should shortly see the status `MQTT connection: CONNECTED`.

   ```bash
   CREDS=`cat config/creds.json` \
   && ESCAPED_CREDS=`printf "%q" "${CREDS}"` \
   && ~/Android/Sdk/platform-tools/adb shell am start \
      -a android.intent.action.VIEW \
      -n com.aws.iotfleetwise/.MainActivity \
      -e credentials "${ESCAPED_CREDS}"
   ```

1. Run the following command to create an AWS IoT FleetWise campaign to collect GPS and AAOS VHAL
   data and send it to Amazon Timestream. After a short time you should see that the status in the
   app is updated with:
   `Campaign ARNs: arn:aws:iotfleetwise:us-east-1:XXXXXXXXXXXX:campaign/fwdemo-android-XXXXXXXXXX-campaign`.

   ```bash
   ./setup-iotfleetwise.sh
   ```

1. Go to the
   [Amazon Timestream Query editor](https://us-east-1.console.aws.amazon.com/timestream/home?region=us-east-1#query-editor:)
   and run the query suggested by the `setup-iotfleetwise.sh` script, which will be of the form:

   ```
   SELECT * FROM "IoTFleetWiseDB-<TIMESTAMP>"."VehicleDataTable" WHERE vehicleName='fwdemo-android-<TIMESTAMP>' ORDER BY time DESC LIMIT 1000
   ```

1. **Optional:** If you want to clean up the resources created by the `provision.sh` and
   `setup-iotfleetwise.sh` scripts, run the following command. **Note:** this will not delete the
   Amazon Timestream database.

   ```bash
   ./clean-up.sh
   ```

## Android Developer Guide

This guide details how to build the app from source code and use the shared library in your own
Android apps.

### App build guide

An x86_64 Ubuntu 20.04 development machine with 200GB free disk space should be used.

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
       build/x86_64/libaws-iot-fleetwise-edge.so:x86_64 \
       build/arm64-v8a/libaws-iot-fleetwise-edge.so:arm64-v8a \
       build/armeabi-v7a/libaws-iot-fleetwise-edge.so:armeabi-v7a
   ```

1. Build the app:

   ```bash
    mkdir -p tools/android-app/app/src/main/jniLibs \
    && cp -r build/dist/x86_64 build/dist/arm64-v8a build/dist/armeabi-v7a tools/android-app/app/src/main/jniLibs \
    && cp THIRD-PARTY-LICENSES tools/android-app/app/src/main/assets \
    && cd tools/android-app \
    && ANDROID_HOME=/usr/local/android_sdk ./gradlew assemble
   ```

### Shared library interface

The C++ code is compiled into a shared library using the Android NDK. The interface for shared
library can be found in the JNI wrapper class `app/src/main/java/com/aws/iotfleetwise/Fwe.java`. The
shared library can also be used in your app using this interface, which includes a method to ingest
raw CAN frame data.

### Android Automotive system image build guide

This guide details how to build the Android Automotive system image from source, in order for the
app to have access to the privileged VHAL properties. These properties are only accessible to apps
signed with the platform certificate, which in this case will be the
[standard Android test certificate](https://android.googlesource.com/platform/build/+/master/target/product/security/platform.x509.pem).

**Prerequisites:**

- A high performance x86_64 Ubuntu 20.04 development machine (e.g. an `m6a.8xlarge` EC2 instance)
  with 300 GB free storage space.
- A local x86_64 Ubuntu 20.04 machine with [Android Studio](https://developer.android.com/studio)
  and the [AWS CLI](https://docs.aws.amazon.com/cli/latest/userguide/getting-started-install.html)
  installed.

**References:**

- [Android Virtual Device as a Development Platform](https://source.android.com/docs/automotive/start/avd/android_virtual_device)
- [AAOS car AVD tools](https://android.googlesource.com/device/generic/car/+/refs/heads/master/tools)

1. _On the development machine_, install the dependencies for compiling the system image:

   ```bash
   sudo apt update \
   && DEBIAN_FRONTEND=noninteractive sudo -E apt install -y default-jdk zip unzip libncurses5 binutils python curl \
   && sudo curl -o /usr/local/bin/repo https://storage.googleapis.com/git-repo-downloads/repo \
   && sudo chmod +x /usr/local/bin/repo \
   && git config user.name > /dev/null || git config --global user.name "ubuntu" \
   && git config user.email > /dev/null || git config --global user.email "ubuntu@`hostname`" \
   && git config color.ui || git config --global color.ui false
   ```

1. Run the following commands to build the Android Automotive system image. **This will take several
   hours.**

   ```bash
   ANDROID_BRANCH="android12L-release" \
   && REPO_URL="https://android.googlesource.com/platform/manifest" \
   && mkdir $ANDROID_BRANCH \
   && cd $ANDROID_BRANCH \
   && repo init -u $REPO_URL -b $ANDROID_BRANCH --partial-clone \
   && repo sync -c -j8 \
   && source build/envsetup.sh \
   && lunch sdk_car_x86_64-userdebug \
   && m -j`nproc`
   ```

1. Create the file `emu_img_zip.mk` and add the content from
   [here](https://cs.android.com/android/platform/superproject/+/master:device/generic/goldfish/tasks/emu_img_zip.mk),
   then create the Android Virtual Device (AVD) image ZIP file.

   ```bash
   m emu_img_zip
   ```

1. _On your local machine_, run the following to download and unzip the AVD image ZIP file in the
   `~/Android/Sdk/system-images` folder:

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:/home/ubuntu/android12L-release/out/target/product/emulator_car_x86_64/sdk-repo-linux-system-images-eng.ubuntu.zip . \
   && unzip -d ~/Android/Sdk/system-images/car_avd \
      sdk-repo-linux-system-images-eng.ubuntu.zip
   ```

1. Create the file `create_avd_config.sh` and add the content from
   [here](https://android.googlesource.com/device/generic/car/+/refs/heads/master/tools/create_avd_config.sh),
   then create the AVD config file (with display settings 213 DPI, 1920x1080 resolution, and 3584 MB
   of memory):

   ```bash
   bash create_avd_config.sh \
      car_avd \
      ~ \
      $HOME/Android/Sdk/system-images/car_avd/x86_64/ \
      213 \
      1920 \
      1080 \
      3584 \
      x86_64
   ```

1. Run the emulator, which will start AAOS:

   ```bash
   ANDROID_SDK_ROOT=~/Android/Sdk ~/Android/Sdk/emulator/emulator -avd car_avd
   ```

1. Copy the platform key and certificate from the development machine (this is the
   [standard Android test certificate](https://android.googlesource.com/platform/build/+/master/target/product/security/platform.x509.pem)):

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:/home/ubuntu/android12L-release/build/target/product/security/platform.x509.pem .
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:/home/ubuntu/android12L-release/build/target/product/security/platform.pk8 .
   ```

1. Download the app from GitHub and re-sign it with the platform key, so that the app has access to
   the AAOS VHAL properties with privileged level permissions:

   ```bash
   curl -o aws-iot-fleetwise-edge-original.apk https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge.apk \
   && `ls -d ~/Android/Sdk/build-tools/* | tail -n -1`/apksigner sign \
      --key platform.pk8 \
      --cert platform.x509.pem \
      --out aws-iot-fleetwise-edge.apk \
      aws-iot-fleetwise-edge-original.apk
   ```

1. Install the app using `adb`:

   ```bash
   ~/Android/Sdk/platform-tools/adb install aws-iot-fleetwise-edge.apk
   ```

1. You can now follow the [this section](#provision-credentials-and-collect-data) to provision
   credentials and collect data.

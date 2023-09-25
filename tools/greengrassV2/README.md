# FWE with AWS IoT Greengrass V2 IPC

The Reference Implementation for AWS IoT FleetWise ("FWE") provides an integration with the AWS IoT
Greengrass V2 IPC client. It enables FWE to be deployed as a Greengrass v2 Component and allows it
to connect to AWS IoT Core via the Greengrass IPC mechanism. Authentication with IoT Core is handled
by Greengrass, simplifying the configuration of IoT FleetWise.

## Compile the FWE with Greengrass V2 feature

Enable the compile flag `-DFWE_FEATURE_GREENGRASSV2=On`:

```bash
mkdir build && cd build \
&& cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DFWE_STATIC_LINK=On \
    -DBUILD_TESTING=Off \
    -DFWE_FEATURE_GREENGRASSV2=On \
    .. \
&& make install -j`nproc` \
&& cd ..
```

## Install IoT Greengrass core in local machine

Install IoT Greengrass v2 core on the development machine, by following these instructions:
https://docs.aws.amazon.com/greengrass/v2/developerguide/getting-started.html#install-greengrass-v2

## Configure the FWE as the deployment of Greengrass V2

Run the following command to create the configuration file for IoT FleetWise. Replace
`<VEHICLE_NAME>` with the thing name that you used above to register the IoT Greengrass device:

```bash
mkdir -p tools/greengrassV2/artifacts/com.amazon.aws.IoTFleetWise/1.0.0/
./tools/configure-fwe.sh \
    --input-config-file ./configuration/static-config.json \
    --output-config-file tools/greengrassV2/artifacts/com.amazon.aws.IoTFleetWise/1.0.0/config.json \
    --vehicle-name <VEHICLE_NAME>
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
    tools/greengrassV2/artifacts/com.amazon.aws.IoTFleetWise/1.0.0/
sudo /greengrass/v2/bin/greengrass-cli deployment create \
    --recipeDir tools/greengrassV2/recipes \
    --artifactDir tools/greengrassV2/artifacts \
    --merge "com.amazon.aws.IoTFleetWise=1.0.0"
```

View the logs of running component as follows:

```bash
sudo tail -f /greengrass/v2/logs/com.amazon.aws.IoTFleetWise.log
```

## Testing

To test IoT FleetWise you can now follow the instructions in the Edge Agent Developer Guide to run
the cloud demo script.

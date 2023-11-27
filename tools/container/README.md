# Containerized FWE

A containerized version of FWE is built using Github Actions, and is available from AWS ECR Public
Gallery: https://gallery.ecr.aws/aws-iot-fleetwise-edge/aws-iot-fleetwise-edge

The version of FWE with ROS2 support is available at:
https://gallery.ecr.aws/aws-iot-fleetwise-edge/aws-iot-fleetwise-edge-ros2

## Running the Container

Use `tools/provision.sh` to provision credentials, then run the container by:

1. Mounting the MQTT certificate and private key at `/etc/aws-iot-fleetwise/certificate.pem` and
   `/etc/aws-iot-fleetwise/private-key.key` respectively.
2. Pass the following environment variables to the container:

- `VEHICLE_NAME`: Vehicle name
- `ENDPOINT_URL`: MQTT Endpoint URL
- `CAN_BUS0`: CAN bus 0, e.g. `vcan0`

For example:

```bash
docker run \
    -ti \
    --network=host \
    -v <PATH_TO_CERTIFICATE_FILE>:/etc/aws-iot-fleetwise/certificate.pem \
    -v <PATH_TO_PRIVATE_KEY_FILE>:/etc/aws-iot-fleetwise/private-key.key \
    --env VEHICLE_NAME=<VEHICLE_NAME> \
    --env ENDPOINT_URL=<ENDPOINT_URL> \
    --env CAN_BUS0=<CAN_BUS0> \
    public.ecr.aws/aws-iot-fleetwise-edge/aws-iot-fleetwise-edge
```

3. Alternatively credentials can be directly passed as environment variables to the container:

For example:

```bash
docker run \
    -ti \
    --network=host \
    --env CERTIFICATE="-----BEGIN-----\nXXXXX\n-----END-----\n" \
    --env PRIVATE_KEY="-----BEGIN-----\nXXXXX\n-----END-----\n" \
    --env VEHICLE_NAME=<VEHICLE_NAME> \
    --env ENDPOINT_URL=<ENDPOINT_URL> \
    --env CAN_BUS0=<CAN_BUS0> \
    public.ecr.aws/aws-iot-fleetwise-edge/aws-iot-fleetwise-edge
```

## Running the Container with ROS2 support

Use `tools/provision.sh` to provision credentials, making sure to use `--creds-role-alias` to pass
your IoT Credentials Provider role alias in order to enable ROS2 support, then run the container by:

1. Mounting the MQTT certificate and private key at `/etc/aws-iot-fleetwise/certificate.pem` and
   `/etc/aws-iot-fleetwise/private-key.key` respectively.
2. Pass the following environment variables to the container:

- `VEHICLE_NAME`: Vehicle name
- `ENDPOINT_URL`: MQTT Endpoint URL
- `CREDS_ENDPOINT_URL`: IoT Credentials Provider Endpoint URL
- `CREDS_ROLE_ALIAS`: IoT Credentials Provider role alias
- `RAW_DATA_BUFFER_SIZE`: Raw data buffer size in bytes

For example:

```bash
docker run \
    -ti \
    --network=host \
    -v <PATH_TO_CERTIFICATE_FILE>:/etc/aws-iot-fleetwise/certificate.pem \
    -v <PATH_TO_PRIVATE_KEY_FILE>:/etc/aws-iot-fleetwise/private-key.key \
    --env VEHICLE_NAME=<VEHICLE_NAME> \
    --env ENDPOINT_URL=<ENDPOINT_URL> \
    --env CREDS_ENDPOINT_URL=<CREDS_ENDPOINT_URL> \
    --env CREDS_ROLE_ALIAS=<CREDS_ROLE_ALIAS> \
    --env RAW_DATA_BUFFER_SIZE=<RAW_DATA_BUFFER_SIZE> \
    public.ecr.aws/aws-iot-fleetwise-edge/aws-iot-fleetwise-edge-ros2
```

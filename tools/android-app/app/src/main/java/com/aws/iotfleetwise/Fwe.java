// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

package com.aws.iotfleetwise;

import android.content.res.AssetManager;

public class Fwe {
    /**
     * Run FWE. This will block until the `stop` method is called.
     * @param assetManager Android asset manager object, used to access the files in `assets/`
     * @param vehicleName Name of the vehicle
     * @param endpointUrl AWS IoT Core endpoint URL
     * @param certificate AWS IoT Core certificate
     * @param privateKey AWS IoT Core private key
     * @param mqttTopicPrefix MQTT topic prefix, which should be "$aws/iotfleetwise/"
     * @return Zero on success, non-zero on error
     */
    public native static int run(
            AssetManager assetManager,
            String vehicleName,
            String endpointUrl,
            String certificate,
            String privateKey,
            String mqttTopicPrefix);

    /**
     * Stop FWE
     */
    public native static void stop();

    /**
     * Get the OBD PIDs that should be requested according to the campaign.
     * @return Array of OBD PID identifiers to request
     */
    public native static int[] getObdPidsToRequest();

    /**
     * Set the OBD response to a PID request for a given PID.
     * @param pid PID
     * @param response Array of response bytes received from vehicle
     */
    public native static void setObdPidResponse(int pid, int[] response);

    /**
     * Ingest raw CAN message
     * @param interfaceId Interface identifier, as defined in the static config JSON file
     * @param timestamp Timestamp of the message in milliseconds since the epoch, or zero to use the system time
     * @param messageId CAN message ID in Linux SocketCAN format
     * @param data CAN message data
     */
    public native static void ingestCanMessage(String interfaceId, long timestamp, int messageId, byte[] data);

    /**
     * Set the GPS location
     * @param latitude Latitude
     * @param longitude Longitude
     */
    public native static void setLocation(double latitude, double longitude);

    /**
     * Get a status summary
     * @return Status summary
     */
    public native static String getStatusSummary();

    /**
     * Get the version
     * @return Version
     */
    public native static String getVersion();

    static {
        System.loadLibrary("aws-iot-fleetwise-edge");
    }
}

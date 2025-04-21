# Vision System Data Offline Testing

FWE can be tested offline without an internet connection by leveraging its persistency
functionality. The decoder manifest and collection schemes files normally saved by FWE to disk can
instead be generated using the `protofactory.py` script. The static config file for FWE is then
configured with a dummy MQTT connection, so that when FWE is started it does not connect to the
cloud and instead reads the generated persistency files. Since there is no available connection to
the cloud, when data is collected it is also saved to the persistency directory. In this way FWE can
be tested in a closed-loop scenario without an internet connection.

## Converting ROS2 message information to JSON format

To import ROS2 messages to the required JSON format:

1. Build your project using `colcon` as normal and run `source install/setup.bash`

2. Write a configuration file specifying the messages to add to the decoder manifest in the
   following JSON format, where:

   - `<FULLY_QUALIFIED_NAME>`: The fully-qualified-name of the message in the AWS IoT FleetWise
     signal catalog.
   - `<INTERFACE_ID>`: The interface ID for the ROS2 interface, as specified in the FWE static
     config file.
   - `<TOPIC>`: The ROS2 topic name.
   - `<TYPE>`: The ROS2 package and type name, e.g. `sensor_msgs/msg/Image`.

   ```json
   {
     "messages": [
       {
         "fullyQualifiedName": <FULLY_QUALIFIED_NAME>,
         "interfaceId": <INTERFACE_ID>,
         "topic": <TOPIC>,
         "type": <TYPE>
       },
       ...
     ]
   }
   ```

3. Run the following script to convert the ROS2 message information to decoder manifest format:

   ```bash
   python3 tools/cloud/ros2-to-decoders.py --config <CONFIG_FILE> --output vision-system-data-decoder-manifest.json
   ```

## Generating the decoder manifest and collection schemes files

1. Write a campaign JSON file in the following format, containing one or more collection schemes.

   ```json
   [
     {
       "campaignSyncId": "my_heartbeat_campaign",
       "collectionScheme": {
         "timeBasedCollectionScheme": {
           "periodMs": 5000
         }
       },
       "signalsToCollect": [
         {
           "name": "Vehicle.Location"
         }
       ]
     },
     {
       "campaignSyncId": "my_event_triggered_campaign",
       "collectionScheme": {
         "conditionBasedCollectionScheme": {
           "expression": "Vehicle.Camera.perception.object[0].confidence < 0.5"
         }
       },
       "signalsToCollect": [
         {
           "name": "Vehicle.Camera"
         }
       ]
     }
   ]
   ```

2. Generate the decoder manifest and collection scheme Protobuf files using the following script:

   ```bash
   python3 test/system/testframework/protofactory.py \
     --vision-system-data-json-filename vision-system-data-decoder-manifest.json \
     --campaign-json-filename <CAMPAIGN_FILE> \
     --additional-json-output \
     --decoder-manifest-filename DecoderManifest.bin \
     --collection-schemes-filename CollectionSchemeList.bin
   ```

## Use the generated files with FWE

Copy the output files `DecoderManifest.bin` and `CollectionSchemeList.bin` to the FWE persistency
folder.

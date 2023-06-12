# Custom data source (treating data not on CAN as CAN signals)

> :warning: **Internal code** structures of AWS IoT FleetWise Edge **might be fundamentally
> restructured**, so extending the code might involve significant work when upgrading to newer
> versions. To avoid this problems the external interfaces can be used to hand over data to AWS IoT
> FleetWise Edge over supported communication channels. For example use a small custom separate
> process to read data and put it to a real or a virtual CAN that is then read by AWS IoT FleetWise
> Edge.

Considering the warning this gives a temporary workaround to get the data directly into AWS IoT
FleetWise _without_ ever putting it on a real or virtual SocketCAN interface. In the following we
will read data from a custom data source (like a tty or a file etc.) convert it to a double and pass
it on to be treated like a Signal read on CAN. All this will happen in a separate thread but inside
the AWS IoT FleetWise Edge agent. We will use an example where we read NMEA GPS data from a file and
handle them as if the were received on a CAN. We assume we have a the two signals
`Vehicle.CurrentLocation.Longitude` and `Vehicle.CurrentLocation.Latitude` in our signal catalog and
in a model manifest. For this the API calls UpdateSignalCatalog and CreateModelManifest can be used.
Now we create a special decoder manifest

```json
{
  "name": "IWaveGpsDecoderManifest",
  "description": "Has all the signals that are read over the custom data source from the NMEA data",
  "modelManifestArn": "arn:aws:iotfleetwise:us-east-1:748151249882:model-manifest/IWaveGPSModel",
  "networkInterfaces": [
    {
      "interfaceId": "IWAVE-GPS-CAN",
      "type": "CAN_INTERFACE",
      "canInterface": {
        "name": "iwavegpscan",
        "protocolName": "CAN"
      }
    }
  ],
  "signalDecoders": [
    {
      "fullyQualifiedName": "Vehicle.CurrentLocation.Longitude",
      "type": "CAN_SIGNAL",
      "interfaceId": "IWAVE-GPS-CAN",
      "canSignal": {
        "messageId": 1,
        "isBigEndian": true,
        "isSigned": true,
        "startBit": 0,
        "offset": -2000.0,
        "factor": 0.001,
        "length": 32
      }
    },
    {
      "fullyQualifiedName": "Vehicle.CurrentLocation.Latitude",
      "type": "CAN_SIGNAL",
      "interfaceId": "IWAVE-GPS-CAN",
      "canSignal": {
        "messageId": 1,
        "isBigEndian": true,
        "isSigned": true,
        "startBit": 32,
        "offset": -2000.0,
        "factor": 0.001,
        "length": 32
      }
    }
  ]
}
```

If you want you can copy over other signals collected to this decoder manifest from existing
campaigns.

The custom data source implementation in the edge code must inherit from `CustomDataSource`. For
this example we created the class `IWaveGpsSource`. The new custom data source class should than
have an init function to set parameters. This init function should then be called from
`IoTFleetWiseEngine::connect()` for example like this:

```cpp
if(mIWaveGpsSource->init(
    "/dev/ttyUSB1", // From this file the GPS coordinates will be read in the NMEA line format
    canIDTranslator.getChannelNumericID( "IWAVE-GPS-CAN" ), // "interfaceId": "IWAVE-GPS-CAN"
    1, // "messageId": 1
    32, //"startBit": 32 for latitude
    0 // "startBit": 0 for longitude
    ) &&
    mIWaveGpsSource->connect()
    )
{
    if ( !mCollectionSchemeManagerPtr->subscribeListener(
                        static_cast<IActiveDecoderDictionaryListener *>( mIWaveGpsSource.get() ) ) )
    {
        FWE_LOG_ERROR(" Failed to register the IWaveGps to the CollectionScheme Manager");
        return false;
    }
    mIWaveGpsSource->start();
}
```

Here we hand over the parameters we gave in the decoder manifest. With the interface Id, the message
Id and the start Bit the custom data source can get the signalId if at least one active campaign
uses one of them. For this to work we need add `canIDTranslator.add( "IWAVE-GPS-CAN");` in the end
of the `CAN InterfaceID to InternalID Translator` section of `IoTFleetWiseEngine::connect()`. This
is necessary because we internally use ids instead of the full name. This parameters can also be
read from the static config as the example in `IoTFleetWiseEngine::connect()` shows where the
optional section "iWaveGpsExample" under "staticConfig" will be used so the parameters can be
changed without recompilation.

## ExternalGpsSource

The provided example `ExternalGpsSource` module can be enabled with the `FWE_FEATURE_EXTERNAL_GPS`
build option. This allows the ingestion of GPS data using the custom data source interface from an
in-process source, for example when the FWE code is built as a shared library. For example the
decoder manifest for the latitude and longitude signals can be created as follows:

```json
{
  "name": "ExternalGpsDecoderManifest",
  "modelManifestArn": "arn:aws:iotfleetwise:us-east-1:748151249882:model-manifest/ExternalGPSModel",
  "networkInterfaces": [
    {
      "interfaceId": "EXTERNAL-GPS-CAN",
      "type": "CAN_INTERFACE",
      "canInterface": {
        "name": "externalgpscan",
        "protocolName": "CAN"
      }
    }
  ],
  "signalDecoders": [
    {
      "fullyQualifiedName": "Vehicle.CurrentLocation.Longitude",
      "type": "CAN_SIGNAL",
      "interfaceId": "EXTERNAL-GPS-CAN",
      "canSignal": {
        "messageId": 1,
        "isBigEndian": true,
        "isSigned": true,
        "startBit": 0,
        "offset": -2000.0,
        "factor": 0.001,
        "length": 32
      }
    },
    {
      "fullyQualifiedName": "Vehicle.CurrentLocation.Latitude",
      "type": "CAN_SIGNAL",
      "interfaceId": "EXTERNAL-GPS-CAN",
      "canSignal": {
        "messageId": 1,
        "isBigEndian": true,
        "isSigned": true,
        "startBit": 32,
        "offset": -2000.0,
        "factor": 0.001,
        "length": 32
      }
    }
  ]
}
```

# Implementing a new CustomDataSource

## What to do in the init

Initialize everything you need to collect your custom data like opening file descriptors. You need
to set the filter of the inherited class `CustomDataSource` like this:
`setFilter(canChannel,canRawFrameId);`. After this is set the `CustomDataSource` will start calling
the `pollData()` function, but only if at least one campaign uses one signal from this CAN message.
The interval in which `pollData()` is called (if at lease one related campaign is active) can be
configured by calling `setPollIntervalMs`.

## How to publish data in the pollData

In the `pollData()` function first extract the data in a custom way (like reading a file/tty or a
socket). Then use the start bit of the signals configured in the cloud to get the internal signalId.
This has to be done in the `pollData()` as the signalId could change between two calls to
`pollData()`. The inherited function `getSignalIdFromStartBit` can be used for this. Then use the
signalId to publish the data to `SignalBufferPtr`. This can for example look like this:

```cpp
mSignalBufferPtr->push(CollectedSignal(getSignalIdFromStartBit(mLatitudeStartBit),timestamp,lastValidLatitude));
```

## Debug

First make sure that at least one campaign uses at least on signal of the custom CAN message you
configured using `setFilter(canChannel,canRawFrameId);`. If that is the case you should see the
following line in the log

```
[TRACE] [CustomDataSource.cpp:159] [matchDictionaryToFilter()]: [Dictionary with relevant information for CustomDataSource so waking up]
```

# IWave GPS setup

Setup the iWave device as described in the [iwave-g26-tutorial](iwave-g26-tutorial.md), then check
that you can see NMEA formatted ASCII data when you run `cat < /dev/ttyUSB1`.

The required decoder manifest changes are explained in
[custom-data-source](../custom-data-source.md). If you are using the console to create the decoder
manifest, you can create an interface with Network Interface Id `IWAVE-GPS-CAN` and then import the
dbc file [IWAVE-GPS-CAN.dbc](IWAVE-GPS-CAN.dbc)

# Configuration

In the `staticConfig` section of the config.json the following section can be used to specify the
parameters without recompiling FWE:

```
"iWaveGpsExample": {
    "nmeaFilePath": "/dev/ttyUSB1",
    "canChannel" : "IWAVE-GPS-CAN",
    "canFrameId" : "1",
    "longitudeStartBit" : "0",
    "latitudeStartBit" : "32"
}
```

When configure AWS IoT FleetWise use the `cmake` with the flag `-DFWE_FEATURE_IWAVE_GPS=On` and then
compile using `make` as usual.

# Debug

Like CAN if data is sent to cloud you should see this:

```
[INFO ] [IoTFleetWiseEngine.cpp:914] [doWork()]: [FWE data ready to send with eventID 1644139266 from arn:aws:iotfleetwise:us-east-1:xxxxxxxxxxxx:campaign/IWaveGpsCampaign Signals:70 [2514:13.393196,2514:13.393196,2514:13.393196,2514:13.393196,2514:13.393196,2514:13.393196, ...] first signal timestamp: 1666881551998 raw CAN frames:0 DTCs:0 Geohash:]
```

If the GPS NMEA output it working but gps fix is available you should move to an area with a open
sight to the sky. As long as no GPS fix is available you will see every 10 seconds (assuming you
configured `systemWideLogLevel` to `Trace`):

```
[TRACE] [IWaveGpsSource.cpp:112] [pollData()]: [In the last 10000 millisecond found 10 lines with $GPGGA and extracted 0 valid coordinates from it]
```

As soon as data is available you should see this:

```
[TRACE] [IWaveGpsSource.cpp:112] [pollData()]: [In the last 10000 millisecond found 11 lines with $GPGGA and extracted 11 valid coordinates from it]
```

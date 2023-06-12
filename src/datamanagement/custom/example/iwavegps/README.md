# IWave GPS setup

Setup the iWave device like described in the
[iwave-g26-tutorial](../../../../../docs/iwave-g26-tutorial/iwave-g26-tutorial.md) To enable the GPS
open the file `/etc/ppp/chat/gprs` and add the following (for example above the AT+CPIN line):

```
# Enable GPS
OK AT+QGPS=1
```

to apply the changes you can use `systemctl restart lte`. After that you should see NMEA formatted
ASCII data if you do `cat < /dev/ttyUSB1`

The decoder manifest changes are explained in the README in the parent folder

# Configuration

If you provide in the `staticConfig` section of the config.json file the following section you can
change the parameters without recompiling AWS IoT FleetWise Edge:

```
"iWaveGpsExample": {
    "nmeaFilePath": "/dev/ttyUSB1",
    "canChannel" : "IWAVE-GPS-CAN",
    "canFrameId" : "1",
    "longitudeStartBit" : "0",
    "latitudeStartBit" : "32"
}
```

When configure AWS IoT FleetWise use the `cmake` with the flags
`-DFWE_FEATURE_CUSTOM_DATA_SOURCE=On -DFWE_FEATURE_IWAVE_GPS=On` and then compile using `make` as
usual.

# Debug

Like CAN if data is sent to cloud you should see this:

```
[INFO ] [IoTFleetWiseEngine.cpp:914] [doWork()]: [FWE data ready to send with eventID 1644139266 from arn:aws:iotfleetwise:us-east-1:748151249882:campaign/IWaveGpsCampaign Signals:70 [2514:13.393196,2514:13.393196,2514:13.393196,2514:13.393196,2514:13.393196,2514:13.393196, ...] first signal timestamp: 1666881551998 raw CAN frames:0 DTCs:0 Geohash:]
```

If the GPS NMEA output it working but gps fix is available you should move to an area with a open
sight to the sky. As long as no GPS fix is available you will see every 10 seconds:

```
[TRACE] [IWaveGpsSource.cpp:112] [pollData()]: [In the last 10000 millisecond found 10 lines with $GPGGA and extracted 0 valid coordinates from it]
```

As soon as data is available you should see this:

```
[TRACE] [IWaveGpsSource.cpp:112] [pollData()]: [In the last 10000 millisecond found 11 lines with $GPGGA and extracted 11 valid coordinates from it]
```

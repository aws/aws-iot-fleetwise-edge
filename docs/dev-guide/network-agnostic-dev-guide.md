# Network agnostic developer guide

<!-- prettier-ignore -->
> [!NOTE]
> This is a "gated" feature of AWS IoT FleetWise for which you will need to request access. See
> [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html)
> for more information, or contact the
> [AWS Support Center](https://console.aws.amazon.com/support/home#/).

Network agnostic data collection and actuator commands (NADC) is a feature of AWS IoT FleetWise that
enables the collection of data from sensors and the remote command of actuators that are accessible
via any arbitrary network interface.

The sensors or actuators are modelled as signals in the signal catalog in the same way as CAN and
OBD signals, but the decoder manifest for these signals instead passes an arbitrary 'decoder' string
to the edge to map the signal to a input or output at the network layer. The decoder string may for
example be an identifier such as the fully-qualified-name (FQN) of the signal. The decoder string
can however also convey other parameters, e.g. CSV data, to perform decoding of the signal.

This guide details examples of NADC in the Reference Implementation for AWS IoT FleetWise (FWE), and
details the steps needed to add your own sensors or actuators using the NADC feature.

## Examples of NADC in FWE

### AAOS VHAL

The [`AaosVhalSource`](../../include/aws/iotfleetwise/AaosVhalSource.h) **does not** use the FQN as
the decoder string, but rather passes a CSV of parameters for obtaining each Android Automotive
(AAOS) VHAL vehicle property.

The corresponding JSON files for use with the cloud APIs are:

- [`custom-nodes-aaos-vhal.json`](../../tools/android-app/cloud/custom-nodes-aaos-vhal.json)
- [`network-interface-custom-aaos-vhal.json`](../../tools/android-app/cloud/network-interface-custom-aaos-vhal.json)
- [`custom-decoders-aaos-vhal.json`](../../tools/android-app/cloud/custom-decoders-aaos-vhal.json)

See [the Android Automotive guide](../../tools/android-app/README.md#android-automotive-user-guide)
for step-by-step instructions on running this example.

### CAN actuators

The [`CanCommandDispatcher`](../../include/aws/iotfleetwise/CanCommandDispatcher.h) implements
actuators controlled via CAN bus. The `CanCommandDispatcher` is registered with the
[`ActuatorCommandManager`](../../include/aws/iotfleetwise/ActuatorCommandManager.h) which identifies
actuator signals by FQN.

The corresponding JSON files for use with the cloud APIs are:

- [`custom-nodes-can-actuators.json`](../../tools/cloud/custom-nodes-can-actuators.json)
- [`network-interface-custom-can-actuators.json`](../../tools/cloud/network-interface-custom-can-actuators.json)
- [`custom-decoders-can-actuators.json`](../../tools/cloud/custom-decoders-can-actuators.json)

See [the CAN actuators guide](./can-actuators-dev-guide.md) for step-by-step instructions on running
this example.

### Location

The [`IWaveGpsSource`](../../include/aws/iotfleetwise/IWaveGpsSource.h) and the
[`ExternalGpsSource`](../../include/aws/iotfleetwise/ExternalGpsSource.h) both use the
[`NamedSignalDataSource`](../../include/aws/iotfleetwise/NamedSignalDataSource.h) to ingest location
data. The `ExternalGpsSource` is exposed as a public class member of
[`IoTFleetWiseEngine](../../include/aws/iotfleetwise/IoTFleetWiseEngine.h) allowing ingestion of
position information from outside FWE, for example from Android.

The corresponding JSON files for use with the cloud APIs are:

- [`custom-nodes-location.json`](../../tools/cloud/custom-nodes-location.json)
- [`network-interface-custom-location.json`](../../tools/cloud/network-interface-custom-location.json)
- [`custom-decoders-location.json`](../../tools/cloud/custom-decoders-location.json)

See [the iWave guide](../iwave-g26-tutorial/iwave-g26-tutorial.md) or
[the Android guide](../../tools/android-app/README.md) for a step-by-step instructions on running
these examples.

### `MULTI_RISING_EDGE_TRIGGER`

The
[CustomFunctionMultiRisingEdgeTrigger](../../include/aws/iotfleetwise/CustomFunctionMultiRisingEdgeTrigger.h)
module implements a [custom function](./custom-function-dev-guide.md) that produces signal data for
direct collection via the custom function
[`conditionEndCallback`](./custom-function-dev-guide.md#interface-conditionendcallback) interface.
The signal data is modelled as a signal with FQN `Vehicle.MultiRisingEdgeTrigger`. The
[`NamedSignalDataSource`](../../include/aws/iotfleetwise/NamedSignalDataSource.h) is used to obtain
the signal ID of this signal.

The corresponding JSON files for use with the cloud APIs are:

- [`custom-nodes-multi-rising-edge-trigger.json`](../../tools/cloud/custom-nodes-multi-rising-edge-trigger.json)
- [`network-interface-custom-named-signal.json`](../../tools/cloud/network-interface-custom-named-signal.json)
- [`custom-decoders-multi-rising-edge-trigger.json`](../../tools/cloud/custom-decoders-multi-rising-edge-trigger.json)

See
[the custom function developer guide](./custom-function-dev-guide.md#custom-function-multi_rising_edge_trigger)
for more information.

### SOME/IP

FWE includes example FIDL and FDEPL files for SOME/IP sensor and actuator signals:
[`ExampleSomeipInterface.fidl`](../../interfaces/someip/fidl/ExampleSomeipInterface.fidl),
[`ExampleSomeipInterface.fdepl`](../../interfaces/someip/fidl/ExampleSomeipInterface.fdepl). The
[CommonAPI](https://covesa.github.io/capicxx-core-tools/) proxy for reading the sensor values and
the stubs for performing the actuator commands are implemented in
[`ExampleSomeipInterfaceWrapper`](../../include/aws/iotfleetwise/ExampleSomeipInterfaceWrapper.h).
Data collection from SOME/IP is performed by
[`SomeipDataSource`](../../include/aws/iotfleetwise/SomeipDataSource.h), which uses the
[`NamedSignalDataSource`](../../include/aws/iotfleetwise/NamedSignalDataSource.h) to identify the
signals by FQN and ingest the signal values. Remote command actuation for SOME/IP is performed by
[`SomeipCommandDispatcher`](../../include/aws/iotfleetwise/SomeipCommandDispatcher.h). The
`SomeipCommandDispatcher` is registered with the
[`ActuatorCommandManager`](../../include/aws/iotfleetwise/ActuatorCommandManager.h) which identifies
actuator signals by FQN.

The corresponding JSON files for use with the cloud APIs are:

- [`custom-nodes-someip.json`](../../tools/cloud/custom-nodes-someip.json)
- [`network-interface-custom-someip.json`](../../tools/cloud/network-interface-custom-someip.json)
- [`custom-decoders-someip.json`](../../tools/cloud/custom-decoders-someip.json)

See [the SOME/IP demo documentation](./edge-agent-dev-guide-someip.md) for a step-by-step guide to
running this example.

### UDS DTC

The [`RemoteDiagnosticDataSource`](../../include/aws/iotfleetwise/RemoteDiagnosticDataSource.h)
collects DTC data and provides it for ingestion via
[`NamedSignalDataSource`](../../include/aws/iotfleetwise/NamedSignalDataSource.h) as a signal with
FQN `Vehicle.ECU1.DTC_INFO`.

The corresponding JSON files for use with the cloud APIs are:

- [`custom-nodes-uds-dtc.json`](../../tools/cloud/custom-nodes-uds-dtc.json)
- [`network-interface-custom-uds-dtc.json`](../../tools/cloud/network-interface-custom-uds-dtc.json)
- [`custom-decoders-uds-dtc.json`](../../tools/cloud/custom-decoders-uds-dtc.json) See
  [the UDS DTC Example developer guide](./edge-agent-uds-dtc-dev-guide.md) for more information.

## Implementing your own sensors and actuators

### Sensor Data Collection

> In this section data will be ingested for signals based on their FQN, hence the standard
> [`NamedSignalDataSource`](../../include/aws/iotfleetwise/NamedSignalDataSource.h) will be used
> directly. For more complex interfaces, for example when additional configuration parameters such
> as IP address are needed, a custom network interface type will need to be added. Refer to
> [`MyCounterDataSource.h`](../../examples/network_agnostic_data_collection/MyCounterDataSource.h)
> or the examples above to see how this is achieved.

> See the
> [`network_agnostic_data_collection` example](../../examples/network_agnostic_data_collection/README.md)
> for a fully working example of the following.

First, adjust the static configuration file of FWE to add the `namedSignalInterface` interface. The
interface ID will need to match the ID used with the cloud APIs, here it is `NAMED_SIGNAL`.

Open your config file (typically named `config-0.json`) and add the network interface to it. (You
can also do this during the provisioning step, by passing option `--enable-named-signal-interface`
to [`tools/configure-fwe.sh`](../../tools/configure-fwe.sh).)

```json
"networkInterfaces": [
  ...
  {
    "interfaceId": "NAMED_SIGNAL",
    "type": "namedSignalInterface"
  }
  ...
]
```

Next, you need to feed the signal values into the FWE signal buffers. To do that, you use an
existing class called `NamedSignalDataSource`. This class exposes a function which will help us
insert the data we receive from our sensor into FWE's signal buffers.

You can create you own data source that has, for example, a worker thread that captures sensor data,
and injects the data using the `NamedSignalDataSource` into FWE's signal buffers. Of course, any
other type of data acquisition could be implemented here. This example simply uses an existing
`NamedSignalDataSource` that is already initialized as part of the `IoTFleetWiseEngine` bootstrap
code, generates the data, and injects it into the signal buffers.

```cpp
// Generates random Lat/Long signal values
std::random_device rd;
std::mt19937 longGen( rd() );
std::mt19937 latGen( rd() );
std::uniform_real_distribution<> longDist( -180.0, 180.0 );
std::uniform_real_distribution<> latDist( -90.0, 90.0 );
while ( !locationThreadShouldStop )
{
    std::vector<std::pair<std::string, Aws::IoTFleetWise::DecodedSignalValue>> values;
    auto longitude = longDist( longGen );
    auto latitude = latDist( latGen );
    values.emplace_back(
        "Vehicle.CurrentLocation.Longitude",
        Aws::IoTFleetWise::DecodedSignalValue( longitude, Aws::IoTFleetWise::SignalType::DOUBLE ) );
    values.emplace_back(
        "Vehicle.CurrentLocation.Latitude",
        Aws::IoTFleetWise::DecodedSignalValue( latitude, Aws::IoTFleetWise::SignalType::DOUBLE ) );
    // This is the API used to inject the data into the Signal Buffers
    // Passing zero as the timestamp will use the current system time
    engine.mNamedSignalDataSource->ingestMultipleSignalValues( 0, values );
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
}
```

FWE will recognize these two signals and buffer the values. You can go ahead and create a data
collection campaign that acquires those two signals. To do that, use the `create-campaign` cloud API
to collect the signals `Vehicle.CurrentLocation.Longitude` and `Vehicle.CurrentLocation.Latitude`.

### Remote Commands

> See the
> [`network_agnostic_actuator_commands` example](../../examples/network_agnostic_actuator_commands/README.md)
> for a fully working example of the following.

First, adjust the static configuration file of FWE to add a new `acCommandInterface` interface. The
interface ID will need to match the ID used with the cloud APIs, here it is `AC_ACTUATORS`.

Open your config file (typically named `config-0.json`) and add the network interface to it.

```json
"networkInterfaces": [
  ...
  {
    "interfaceId": "AC_ACTUATORS",
    "type": "acCommandInterface"
  }
  ...
]
```

After you have created above an actuator for the AC Controls and declared it in the signal catalog,
model manifest and decoder manifest. Now, write some code that will run when a command to change the
actuator state is sent remotely.

First, create a command dispatcher that is called whenever the system receives a command:

```cpp
// Make sure you implement the ICommandDispatcher
class AcCommandDispatcher : public Aws::IoTFleetWise::ICommandDispatcher
{
public:
    /**
     * @brief Initializer command dispatcher with its associated underlying vehicle network / service
     * @return True if successful. False otherwise.
     */
    bool init() override;

// This method will need to implement the actual command execution provided
// the actuator name and the value
    /**
     * @brief set actuator value
     * @param actuatorName Actuator name
     * @param signalValue Signal value
     * @param commandId Command ID
     * @param issuedTimestampMs Timestamp of when the command was issued in the cloud in ms since
     * epoch.
     * @param executionTimeoutMs Relative execution timeout in ms since `issuedTimestampMs`. A value
     * of zero means no timeout.
     * @param notifyStatusCallback Callback to notify command status
     */
    void setActuatorValue( const std::string &actuatorName,
                           const Aws::IoTFleetWise::SignalValueWrapper &signalValue,
                           const Aws::IoTFleetWise::CommandID &commandId,
                           Aws::IoTFleetWise::Timestamp issuedTimestampMs,
                           Aws::IoTFleetWise::Timestamp executionTimeoutMs,
                           Aws::IoTFleetWise::NotifyCommandStatusCallback notifyStatusCallback ) override;
};
```

An example implementation can be :

```cpp
bool
AcCommandDispatcher::init()
{
    return true;
}

void
AcCommandDispatcher::setActuatorValue( const std::string &actuatorName,
                                       const Aws::IoTFleetWise::SignalValueWrapper &signalValue,
                                       const Aws::IoTFleetWise::CommandID &commandId,
                                       Aws::IoTFleetWise::Timestamp issuedTimestampMs,
                                       Aws::IoTFleetWise::Timestamp executionTimeoutMs,
                                       Aws::IoTFleetWise::NotifyCommandStatusCallback notifyStatusCallback )
{
    // Here invoke your actuation
    FWE_LOG_INFO( "Actuator " + actuatorName + " executed successfully for command ID " + commandId );
    notifyStatusCallback(
        Aws::IoTFleetWise::CommandStatus::SUCCEEDED, Aws::IoTFleetWise::REASON_CODE_UNSPECIFIED, "Success" );
}
```

Finally, register the command dispatcher with `ActuatorCommandManager`. For that use the
`setNetworkInterfaceConfigHook`, see the example
[here](../../examples/network_agnostic_actuator_commands/main.cpp).

```cpp
static const std::string AC_COMMAND_INTERFACE_TYPE = "acCommandInterface";
// ....
else if ( interfaceType == AC_COMMAND_INTERFACE_TYPE )
{
// Here we create and register our command dispatcher
    acCommandDispatcher =std::make_shared<AcCommandDispatcher>();
// All commands on actuators defined in this interface will be routed to the AC command dispatcher
    if ( !engine.mActuatorCommandManager->registerDispatcher( interfaceId, acCommandDispatcher ) )
    {
        return false;
    }
```

Now, compile the software (make sure you add your headers and C++ files to the CMake file), and go
back to the Cloud APIs to test a command on this actuator.

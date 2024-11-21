# Adding custom `.fidl` file

Let there be two files that we are bringing in for adding SOME/IP signals:

- `custom_someip.fidl`
- `custom_someip.fdepl`

Copy

- `custom_someip.fidl` to
  [`ExampleSomeipInterface.fidl`](../../interfaces/someip/fidl/ExampleSomeipInterface.fidl)
- `custom_someip.fdepl` to
  [`ExampleSomeipInterface.fdepl`](../../interfaces/someip/fidl/ExampleSomeipInterface.fdepl)

**Follow the steps to bring these signals for being used with the AWS IoT FleetWise**

1. Add `onChange` handler in
   [`ExampleSomeipInterfaceStubImpl.cpp`](../../tools/someipigen/src/ExampleSomeipInterfaceStubImpl.cpp)

   Go to the
   [`src/ExampleSomeipInterfaceStubImpl.cpp`](../../tools/someipigen/src/ExampleSomeipInterfaceStubImpl.cpp)
   and add an initial value for the signal in the class constructor, and then:

   - Either implement the SOME/IP stub functions for methods using `getValueAsync` and
     `setValueAsync`
   - OR add an `onChange` handler for attributes

   1. Using `getValueAsync` and `setValueAsync`

      Example: if you have SOME/IP methods `getTemperature` and `setTemperature`, and you want them
      to set and get a signal named `Temperature` with type `int32_t`, you would add the following
      in `ExampleSomeipInterfaceStubImpl.cpp`:

      ```cpp
      // Add the initial value in the constructor:
      mSignals["Temperature"] = Signal( boost::any( static_cast<int32_t>( 0 ) ) );

      // Add the stub implementation for `getTemperature` method:
      void
      ExampleSomeipInterfaceStubImpl::getTemperature(
          const std::shared_ptr<CommonAPI::ClientId> client,
          getSpeedReply_t reply )
      {
          (void)client;
          getValueAsync( "Temperature", reply );
      }

      // Add the stub implementation for `setTemperature` method:
      void
      ExampleSomeipInterfaceStubImpl::setTemperature(
          const std::shared_ptr<CommonAPI::ClientId> client,
          int32_t value,
          setTemperatureReply_t reply )
      {
          (void)client;
          setValueAsync( "Temperature", value, reply );
      }
      ```

   1. Using `onChange` handler for attributes

      Example: if you have a SOME/IP attribute `temperature`, and you want to link it to a signal
      named `Temperature` with type `int32_t`, you would add the following in
      `ExampleSomeipInterfaceStubImpl.cpp`:

      ```cpp
      // Add the initial value and onChanged handler in the constructor:
      mSignals["Temperature"] = Signal( boost::any(static_cast<int32_t>( 0 ) ), [this](){
          fireTemperatureAttributeChanged( boost::any_cast<int32_t>( mSignals["Temperature"].value ) );
      } );
      ```

1. Make changes to the existing [`SomeipDataSource.h`](../../src/SomeipDataSource.h). Replace the
   current signals with the signals in `custom_someip.fidl` in a similar way as already implemented
   in the `SomeipDataSource.h`.

   For example lets consider we are adding a signal `Temperature` of type `INT32` :

   ```cpp
   uint32_t mTemperatureSubscription{};
   bool mLastTemperatureValAvailable{};
   int32_t mLastTemperatureVal{};
   void pushTemperatureValue( const int32_t &val );
   ```

1. Make changes to the [`SomeipDataSource.cpp`](../../src/SomeipDataSource.cpp)

   This file ingests the data to FWE. Follow the current settings of the signals in
   `SomeipDataSource.cpp` to setup our own signals to be collected by AWS IoT FleetWise.

   In `SomeipDataSource::~SomeipDataSource()` which is a destructor add a condition to make your
   signal subscription unavailable under the condition `if ( mProxy )`.

   For example:

   ```cpp
   if ( mTemperatureSubscription != 0 )
   {
       mProxy->getTemperatureAttribute().getChangedEvent().unsubscribe( mTemperatureSubscription );
   }
   ```

   Add a function `void SomeipDataSource::pushXXXXValue( const YYYY &val )` to ingest value to your
   signals.

   For example:

   ```cpp
   void
   SomeipDataSource::pushTemperatureValue( const int32_t &val )
   {
       mNamedSignalDataSource->ingestSignalValue(
           0, "Vehicle.ExampleSomeipInterface.Temperature", DecodedSignalValue{ val, SignalType::INT32 } );
   }
   ```

   Under `bool SomeipDataSource::init()` define `mXXXXSubscription`.

   For example:

   ```cpp
   mTemperatureSubscription =
       mProxy->getTemperatureAttribute().getChangedEvent().subscribe( [this]( const int32_t &val ) {
           std::lock_guard<std::mutex> lock( mLastValMutex );
           mLastTemperatureVal = val;
           mLastTemperatureValAvailable = true;
           pushTemperatureValue( val );
       } );
   ```

   Under the if condition `if ( mCyclicUpdatePeriodMs > 0 )` add checks for proxy availability
   similar to the already present implementation.

   For example:

   ```cpp
   while ( !mShouldStop ){
       {
           std::lock_guard<std::mutex> lock( mLastValMutex );
           if ( !mProxy->isAvailable() )
           {
               mLastTemperatureValAvailable = false;
           }
           else{
               if ( mLastTemperatureValAvailable )
               {
                   pushTemperatureValue( mLastTemperatureVal );
               }
           }
       }
       std::this_thread::sleep_for( std::chrono::milliseconds( mCyclicUpdatePeriodMs ) );
   }
   ```

1. Add signals with initial value to [`signals.json`](../../tools/someipigen/signals.json)

   For example:

   ```cpp
   "Vehicle.ExampleSomeipInterface.Temperature": 1
   ```

1. Add signals to [`custom-decoders-someip.json`](../../tools/cloud/custom-decoders-someip.json)
   with proper configurations as current implementation.

   For example:

   ```json
   {
     "fullyQualifiedName": "Vehicle.ExampleSomeipInterface.Temperature",
     "interfaceId": "SOMEIP",
     "type": "CUSTOM_DECODING_SIGNAL",
     "customDecodingSignal": {
       "id": "Vehicle.ExampleSomeipInterface.Temperature"
     }
   }
   ```

1. Add signal to [`custom-nodes-someip.json`](../../tools/cloud/custom-nodes-someip.json)

   Add your signals coming from sensor or an actuator.

   For example:

   ```json
   {
     "sensor": {
       "fullyQualifiedName": "Vehicle.ExampleSomeipInterface.Temperature",
       "description": "Vehicle.ExampleSomeipInterface.Temperature",
       "dataType": "INT32"
     }
   }
   ```

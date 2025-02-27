# Someipigen

#### Adding New Signals

Adding new signals requires a manual step after the code generation. You need to go to the
`src/ExampleSomeipInterfaceStubImpl.cpp` and add an initial value for the signal in the class
constructor, and then either implement the SOME/IP stub functions for methods using `getValueAsync`
and `setValueAsync`, or add an `onChange` handler for attributes.

For example, if you have SOME/IP methods `getSpeed` and `setSpeed`, and you want them to set and get
a signal named `Speed` with type `int32_t`, you would add the following in
`ExampleSomeipInterfaceStubImpl.cpp`:

```cpp
// Add the initial value in the constructor:
mSignals["Speed"] = Signal( boost::any( static_cast<int32_t>( 0 ) ) );

// Add the stub implementation for `getSpeed` method:
void
ExampleSomeipInterfaceStubImpl::getSpeed(
    const std::shared_ptr<CommonAPI::ClientId> client,
    getSpeedReply_t reply )
{
    (void)client;
    getValueAsync( "Speed", reply );
}

// Add the stub implementation for `setSpeed` method:
void
ExampleSomeipInterfaceStubImpl::setSpeed(
    const std::shared_ptr<CommonAPI::ClientId> client,
    int32_t value,
    setSpeedReply_t reply )
{
    (void)client;
    setValueAsync( "Speed", value, reply );
}
```

For example, if you have a SOME/IP attribute `temperature`, and you want to link it to a signal
named `Temperature` with type `int32_t`, you would add the following in
`ExampleSomeipInterfaceStubImpl.cpp`:

```cpp
// Add the initial value and onChanged handler in the constructor:
mSignals["Temperature"] = Signal( boost::any( static_cast<int32_t>( 0 ) ), [this](){
    fireTemperatureAttributeChanged( boost::any_cast<int32_t>( mSignals["Temperature"].value ) );
} );
```

#### Using the Python Module

To use the Python bindings and work with signal values, you can import the module in your Python
script as follows:

```python
import someipigen

signal_holder = someipigen.SignalManager()
signal_holder.start("local", "commonapi.Someipigen", "service-someipigen")
signal_holder.set_value("example_signal", 42)
value = signal_holder.get_value("example_signal")
print(value)  # Should print 42
```

You can also load and save signal values to and from a JSON file:

```python
# Load signal values from a JSON file
signal_holder.load_values("signals.json")

# Get a signal value by its identifier
value = signal_holder.get_value("Odometer")
print(value)  # Should print "100"

# Set a new value for a signal
signal_holder.set_value("ENGINE_RUNNING", "1")

# Save the updated signal values to a JSON file
signal_holder.save_values("signals.json")
```

Should you experience any difficulties when importing the module, please first verify that the
module's installation path has been added to your `PYTHONPATH` environment variable. Alternatively,
consider installing the module directly within the virtual environment you are utilizing.
Additionally, ensure that the following dependencies are installed:

- vsomeip3
- CommonAPI
- CommonAPI-SomeIP

#### Using the REPL

The `someipigen_repl.py` script provides a REPL for interactive signal manipulation. To use it,
simply run the script from the command line:

```bash
VSOMEIP_CONFIGURATION=vsomeip-config.json COMMONAPI_CONFIG=commonapi-config.ini python3 someipigen_repl.py
```

Once the REPL is running, you can use the following commands:

- `set <signal> <value>`: Set the value of a signal.
- `get <signal>`: Get the value of a signal.
- `save <filename>`: Save the current signal values to a JSON file.
- `load <filename>`: Load signal values from a JSON file.
- `list`: List all available signals.
- `help`: Display the list of available commands.
- `exit` or `quit`: Exit the REPL.

Example session:

```
someipigen$ set Odometer 200
someipigen$ get Odometer
200
someipigen$ save signals.json
someipigen$ load signals.json
someipigen$ exit
```

This interactive environment is useful for testing and manipulating signals on the fly.

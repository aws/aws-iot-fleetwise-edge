# Custom function developer guide

<!-- prettier-ignore -->
> [!NOTE]
> This is a "gated" feature of AWS IoT FleetWise for which you will need to request access. See
> [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html)
> for more information, or contact the
> [AWS Support Center](https://console.aws.amazon.com/support/home#/).

The 'custom function' feature of AWS IoT FleetWise allows customers to define functions at the edge
and call these functions by name from within condition-based campaign expressions.

The functions may take a variable number of arguments of varying type, and may return a single value
of varying type. The supported types are `bool`, `double` or `string`. It is possible to use a
custom function anywhere within a campaign expression, including as an argument to logical or
artithmetic operators, or as an argument to another custom function, i.e. nesting custom function
calls.

Within the campaign expression a custom function is invoked using the keyword `custom_function`
followed by parentheses, containing firstly the name of the custom function to invoke as a string
literal, and then zero or more arguments to the function. Custom function signatures are of the
following format including the function return type, function name, and function argument types and
names:

```
<RETURN_TYPE> custom_function('<FUNCTION_NAME>', <ARGUMENT_TYPE> <ARGUMENT_NAME>, ...);
```

This guide details the example custom functions provided in the Reference Implementation for AWS IoT
FleetWise (FWE), followed by a step-by-step guide to running FWE and sending campaigns to the edge
using the custom functions, and finally a developer guide for developing your own custom functions.

## Example custom functions

### Math custom functions: `abs`, `min`, `max`, `pow`, `log`, `ceil`, `floor`

These example custom functions implement math operations, and have the following function
signatures:

```php
double custom_function('abs', double x);                // Calculates the absolute (modulus) of the argument

double custom_function('min', double x, double y, ...); // Calculates the minimum of two or more arguments

double custom_function('max', double x, double y, ...); // Calculates the maximum of two or more arguments

double custom_function('pow', double x, double y);      // Calculates x to the power y

double custom_function('log', double x, double y);      // Calculates the logarithm of y to the base x

double custom_function('ceil', double x);               // Calculates the 'ceiling', i.e. smallest integer greater than or equal to the argument

double custom_function('floor', double x);              // Calculates the 'floor', i.e. greatest integer less than or equal to the argument
```

For example if you wanted to trigger data collection when the magnitude of a vector signal with
components `Vehicle.MyVectorSignal.x` and `Vehicle.MyVectorSignal.y` is greater than 100, then you
could use the following campaign. I.e. the expression is equivalent to the equation:

$\sqrt{Vehicle.MyVectorSignal.x^2 + Vehicle.MyVectorSignal.y^2} > 100$

```json
{
  "compression": "SNAPPY",
  "collectionScheme": {
    "conditionBasedCollectionScheme": {
      "conditionLanguageVersion": 1,
      "expression": "custom_function('pow', custom_function('pow', $variable.`Vehicle.MyVectorSignal.x`, 2) + custom_function('pow', $variable.`Vehicle.MyVectorSignal.y`, 2), 0.5) > 100",
      "triggerMode": "RISING_EDGE"
    }
  },
  "signalsToCollect": [
    {
      "name": "Vehicle.MyVectorSignal.x"
    },
    {
      "name": "Vehicle.MyVectorSignal.y"
    }
  ]
}
```

### Custom function `MULTI_RISING_EDGE_TRIGGER`

The `MULTI_RISING_EDGE_TRIGGER` example custom function is used to trigger data collection on the
rising edge of one or more Boolean conditions, and capture which of the conditions caused the data
collection. This functionality can be used to monitor many Boolean 'alarm' signals in a single AWS
IoT FleetWise campaign. The function signature is as follows:

```php
bool custom_function('MULTI_RISING_EDGE_TRIGGER',
    string conditionName1, bool condition1, // Condition 1: name and value
    string conditionName2, bool condition2, // Condition 2: name and value
    string conditionName3, bool condition3, // Condition 3: name and value
    ...
);
```

The function takes a variable number of pairs of arguments, with each being the `string` name of a
condition and the `bool` value of that condition. When one or more of the conditions has a rising
edge, i.e. changing from false to true, the function will return true, otherwise it returns false.
Additionally when it returns true, it generates a JSON string containing the condition names that
caused the trigger and adds this to the collected data with the signal name
`Vehicle.MultiRisingEdgeTrigger`. This signal must be added to the signal catalog and decoder
manifest in the normal manner, and then added to the campaign's `signalsToCollect` in order to
capture the trigger source.

For example, if you wanted to trigger data collection on the rising edge of any one of three Boolean
signals: `Vehicle.Alarm1`, `Vehicle.Alarm2`, `Vehicle.Alarm3`, you could create a condition based
campaign using `MULTI_RISING_EDGE_TRIGGER` as follows:

```json
{
  "compression": "SNAPPY",
  "collectionScheme": {
    "conditionBasedCollectionScheme": {
      "conditionLanguageVersion": 1,
      "expression": "custom_function('MULTI_RISING_EDGE_TRIGGER', 'ALARM1', $variable.`Vehicle.Alarm1`, 'ALARM2', $variable.`Vehicle.Alarm2`, 'ALARM3', $variable.`Vehicle.Alarm3`)",
      "triggerMode": "RISING_EDGE"
    }
  },
  "signalsToCollect": [
    {
      "name": "Vehicle.MultiRisingEdgeTrigger"
    }
  ]
}
```

If the signals `Vehicle.Alarm1` and `Vehicle.Alarm3` were to have a rising edge at exactly the same
time, the collected value of `Vehicle.MultiRisingEdgeTrigger` would have the following value to
indicate that these were the cause of the data collection:

```json
["ALARM1", "ALARM3"]
```

### Python custom function

FWE can support either [MicroPython](https://github.com/micropython/micropython) or
[CPython](https://github.com/python/cpython) called via a custom function.

- MicroPython is a partial implementation of Python 3.5 that is configured at compile time to enable
  or disable many features. The configuration for FWE is near to the
  `MICROPY_CONFIG_ROM_LEVEL_MINIMUM` configuration with the following additional features enabled:

  - Import of external modules from the filesystem
  - Built-in module support: `json`, `sys`
  - Double-precision floating point number support
  - Additional error reporting (including line numbers)
  - Compilation of scripts to bytecode
  - Garbage collection

  This means scripts **do not** have network or filesystem access and the 'threading' module is not
  supported.

- CPython is the standard implementation of Python 3, and the interpreter configuration for FWE
  enables all features including filesystem and network access. As such, **THIS IMPLEMENTATION
  SHOULD BE USED FOR TESTING PURPOSES ONLY, AND SHOULD NOT BE USED IN PRODUCTION SYSTEMS.**

The custom function signature for both implementations is identical and as follows:

```php
variant                   // Return value of 'invoke' function, see below
custom_function('python',
    string s3Prefix,      // S3 prefix to download the scripts from
    string moduleName,    // Top-level Python module to import containing the 'invoke' function
    ...                   // Remaining args are passed to 'invoke' function
);
```

The imported Python module must contain a function called `invoke` and can optionally contain a
function called `cleanup`.

- The `invoke` function will be called each time the campaign expression is evaluated. The return
  value can either be a single primitive result of type `None`, `bool`, `float` or `str` (string),
  or it can be a tuple with the first member being the primitive result and the second being a
  `dict` containing collected data. The keys of the `dict` are fully-qualified-name of signals, and
  the values of the `dict` must be string values. See below for an example.

- The `cleanup` function is called when the invocation instance is no longer used. Each invocation
  instance of the custom function is independent of the others, hence it is possible to use the same
  Python module multiple times within the same or different campaigns, and any global variables in
  the scripts will be independent.

Examples:

```python
# Example Python custom function to sum the two arguments


def invoke(a, b):
    return a + b
```

```python
# Example Python custom function to return true on a rising edge with collected data of the number of rising edges

last_level = True
rising_edge_count = 0


def invoke(level):
    global last_level
    global rising_edge_count
    rising_edge = level and not last_level
    last_level = level
    if rising_edge:
        rising_edge_count += 1
        return True, {"Vehicle.RisingEdgeCount": str(rising_edge_count)}
    return False
```

If the example script above were uploaded to an S3 bucket with the prefix
`python-campaign-1/rising_edge_count.py`, then the following campaign could be used to count the
rising edges of signal `Vehicle.DriverDoorOpen` collecting the result in `Vehicle.RisingEdgeCount`:

```json
{
  "compression": "SNAPPY",
  "collectionScheme": {
    "conditionBasedCollectionScheme": {
      "conditionLanguageVersion": 1,
      "expression": "custom_function('python', 'python-campaign-1', 'rising_edge_count', $variable.`Vehicle.DriverDoorOpen`)",
      "triggerMode": "RISING_EDGE"
    }
  },
  "signalsToCollect": [
    {
      "name": "Vehicle.RisingEdgeCount"
    }
  ]
}
```

## Step-by-step guide

This section of the guide goes through building and running FWE with support for the example custom
functions, and creating campaigns using them.

### Prerequisites

- Access to an AWS Account with administrator privileges.
- Your AWS account has access to AWS IoT FleetWise "gated" features. See
  [here](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/fleetwise-regions.html) for
  more information, or contact the
  [AWS Support Center](https://console.aws.amazon.com/support/home#/).
- Logged in to the AWS Console in the `us-east-1` region using the account with administrator
  privileges.
  - Note: if you would like to use a different region you will need to change `us-east-1` to your
    desired region in each place that it is mentioned below.
  - Note: AWS IoT FleetWise is currently available in
    [these](https://docs.aws.amazon.com/general/latest/gr/iotfleetwise.html) regions.
- A local Linux or MacOS machine.

### Launch your development machine

An Ubuntu 22.04 development machine with 200GB free disk space will be required. A local Intel
x86_64 (amd64) machine can be used, however it is recommended to use the following instructions to
launch an AWS EC2 Graviton (arm64) instance. Pricing for EC2 can be found,
[here](https://aws.amazon.com/ec2/pricing/on-demand/).

1. Launch an EC2 Graviton instance with administrator permissions:
   [**Launch CloudFormation Template**](https://us-east-1.console.aws.amazon.com/cloudformation/home?region=us-east-1#/stacks/quickcreate?templateUrl=https%3A%2F%2Faws-iot-fleetwise.s3.us-west-2.amazonaws.com%2Flatest%2Fcfn-templates%2Ffwdev.yml&stackName=fwdev).
1. Enter the **Name** of an existing SSH key pair in your account from
   [here](https://us-east-1.console.aws.amazon.com/ec2/v2/home?region=us-east-1#KeyPairs:).
   1. Do not include the file suffix `.pem`.
   1. If you do not have an SSH key pair, you will need to create one and download the corresponding
      `.pem` file. Be sure to update the file permissions: `chmod 400 <PATH_TO_PEM>`
1. **Select the checkbox** next to _'I acknowledge that AWS CloudFormation might create IAM
   resources with custom names.'_
1. Choose **Create stack**.
1. Wait until the status of the Stack is **CREATE_COMPLETE**; this can take up to five minutes.
1. Select the **Outputs** tab, copy the EC2 IP address, and connect via SSH from your local machine
   to the development machine.

   ```bash
   ssh -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>
   ```

### Obtain the FWE code

1. Run the following _on the development machine_ to clone the latest FWE source code from GitHub.

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge
   ```

### Download or build the FWE binary

**To quickly run the demo**, download the pre-built FWE binary:

- If your development machine is ARM64 (the default if you launched an EC2 instance using the
  CloudFormation template above):

  ```bash
  cd ~/aws-iot-fleetwise-edge \
  && mkdir -p build \
  && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
      https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-arm64.tar.gz  \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz -C build aws-iot-fleetwise-edge
  ```

- If your development machine is x86_64:

  ```bash
  cd ~/aws-iot-fleetwise-edge \
  && mkdir -p build \
  && curl -L -o build/aws-iot-fleetwise-edge.tar.gz \
      https://github.com/aws/aws-iot-fleetwise-edge/releases/latest/download/aws-iot-fleetwise-edge-amd64.tar.gz  \
  && tar -zxf build/aws-iot-fleetwise-edge.tar.gz -C build aws-iot-fleetwise-edge
  ```

**Alternatively if you would like to build the FWE binary from source,** follow these instructions.
If you already downloaded the binary above, skip to the next section.

1. Install the dependencies for FWE:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && sudo -H ./tools/install-deps-native.sh \
       --with-micropython-support \
   && sudo ldconfig
   ```

1. Compile FWE with the custom function examples and MicroPython support:

   ```bash
   ./tools/build-fwe-native.sh \
       --with-custom-function-examples \
       --with-micropython-support
   ```

**Note: If you want to try the CPython integration, you should instead run `install-deps-native.sh`
and `build-fwe-native.sh` with the option `--with-cpython-support` and later run `configure-fwe.sh`
with the option `--enable-cpython`.**

### Install the CAN simulator

1. Run the following command to install the CAN simulator:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && sudo -H ./tools/install-socketcan.sh \
   && sudo -H ./tools/install-cansim.sh
   ```

### Provision FWE

If you **do not** want to evaluate the scripting examples below, skip to the
[next](#provision-fwe-without-scripting-support) section.

#### Provision FWE with scripting support

The scripting examples make use of the
[`CustomFunctionScriptEngine`](../../include/aws/iotfleetwise/CustomFunctionScriptEngine.h) to
download the scripts from an S3 bucket configured in the static configuration file. The custom
function argument `s3Prefix` is used to either download all files under a directory prefix, or if
the prefix ends in `.tar.gz` only that file is downloaded and the archive is extracted. The
`CustomFunctionScriptEngine` will cache downloaded files based on the campaign. If you change the
files in the S3 bucket, you will need to deploy a new campaign for FWE to re-download the files.

1. In order to execute the scripting examples below, run the following commands _on the development
   machine_ to create a new S3 bucket for storing the scripts, and an IAM role and IoT Credentials
   Provider role alias (via a CloudFormation stack) for FWE to access the S3 bucket.

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && REGION="us-east-1" \
   && UUID=`cat /proc/sys/kernel/random/uuid` \
   && DISAMBIGUATOR=${UUID:0:8} \
   && S3_BUCKET_NAME="custom-function-demo-bucket-${DISAMBIGUATOR}" \
   && STACK_NAME="custom-function-demo-credentials-provider-${DISAMBIGUATOR}" \
   && ROLE_ALIAS="custom-function-demo-role-alias-${DISAMBIGUATOR}" \
   && ACCOUNT_ID=`aws sts get-caller-identity | jq -r .Account` \
   && echo "S3_BUCKET_NAME=${S3_BUCKET_NAME}" > tools/cloud/demo.env \
   && aws s3 mb s3://${S3_BUCKET_NAME} --region ${REGION} \
   && aws cloudformation create-stack \
       --region ${REGION} \
       --stack-name ${STACK_NAME} \
       --template-body file://tools/cfn-templates/iot-credentials-provider.yml \
       --parameters ParameterKey=RoleAlias,ParameterValue=${ROLE_ALIAS} \
           ParameterKey=S3BucketName,ParameterValue=${S3_BUCKET_NAME} \
           ParameterKey=IoTCoreRegion,ParameterValue=${REGION} \
           ParameterKey=S3BucketPrefixPattern,ParameterValue='"*"' \
           ParameterKey=S3Actions,ParameterValue='"s3:ListBucket,s3:GetObject"' \
       --capabilities CAPABILITY_AUTO_EXPAND CAPABILITY_NAMED_IAM
   ```

1. Run the following to provision an AWS IoT Thing with credentials:

   ```bash
   mkdir -p build_config \
   && ./tools/provision.sh \
       --region us-east-1 \
       --vehicle-name fwdev-custom-functions \
       --certificate-pem-outfile build_config/certificate.pem \
       --private-key-outfile build_config/private-key.key \
       --endpoint-url-outfile build_config/endpoint.txt \
       --vehicle-name-outfile build_config/vehicle-name.txt \
       --creds-role-alias ${ROLE_ALIAS} \
       --creds-role-alias-outfile build_config/creds-role-alias.txt \
       --creds-endpoint-url-outfile build_config/creds-endpoint.txt \
   && ./tools/configure-fwe.sh \
       --input-config-file configuration/static-config.json \
       --output-config-file build_config/config-0.json \
       --log-color Yes \
       --log-level Trace \
       --vehicle-name `cat build_config/vehicle-name.txt` \
       --endpoint-url `cat build_config/endpoint.txt` \
       --can-bus0 vcan0 \
       --certificate-file `realpath build_config/certificate.pem` \
       --private-key-file `realpath build_config/private-key.key` \
       --creds-role-alias `cat build_config/creds-role-alias.txt` \
       --creds-endpoint-url `cat build_config/creds-endpoint.txt` \
       --persistency-path `realpath build_config` \
       --enable-named-signal-interface \
       --script-engine-bucket-name ${S3_BUCKET_NAME} \
       --script-engine-bucket-region ${REGION} \
       --script-engine-bucket-owner ${ACCOUNT_ID} \
       --enable-micropython
   ```

#### Provision FWE without scripting support

**If you already provisioned FWE with scripting support above, skip to the next section.**

1. Run the following _on the development machine_ to provision an AWS IoT Thing with credentials:

   ```bash
   cd ~/aws-iot-fleetwise-edge \
   && mkdir -p build_config \
   && ./tools/provision.sh \
       --region us-east-1 \
       --vehicle-name fwdev-custom-functions \
       --certificate-pem-outfile build_config/certificate.pem \
       --private-key-outfile build_config/private-key.key \
       --endpoint-url-outfile build_config/endpoint.txt \
       --vehicle-name-outfile build_config/vehicle-name.txt \
   && ./tools/configure-fwe.sh \
       --input-config-file configuration/static-config.json \
       --output-config-file build_config/config-0.json \
       --log-color Yes \
       --log-level Trace \
       --vehicle-name `cat build_config/vehicle-name.txt` \
       --endpoint-url `cat build_config/endpoint.txt` \
       --can-bus0 vcan0 \
       --certificate-file `realpath build_config/certificate.pem` \
       --private-key-file `realpath build_config/private-key.key` \
       --persistency-path `realpath build_config` \
       --enable-named-signal-interface
   ```

### Run FWE

1. Run FWE:

   ```bash
   ./build/aws-iot-fleetwise-edge build_config/config-0.json
   ```

### Run the AWS IoT FleetWise demo script

The instructions below will register your AWS account for AWS IoT FleetWise, create a demonstration
vehicle model, register the virtual vehicle created in the previous section and run a campaign to
collect data from it.

1. Open a new terminal _on the development machine_ and run the following to install the
   dependencies of the demo script:

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
   && sudo -H ./install-deps.sh
   ```

1. Run the following command to generate 'node' and 'decoder' JSON files from the input DBC file:

   ```bash
   python3 dbc-to-nodes.py hscan.dbc can-nodes.json \
   && python3 dbc-to-decoders.py hscan.dbc can-decoders.json
   ```

1. Choose which custom function you would like to evaluate:

   1. To evaluate the math functions, run this command. The campaign treats the signals
      `Vehicle.ECM.DemoEngineTorque` and `Vehicle.ABS.DemoBrakePedalPressure` as components of a
      2-dimensional vector and triggers data collection when the magnitude of the vector is greater
      than 100. See [`campaign-math.json`](../../tools/cloud/campaign-math.json).

      ```bash
      ./demo.sh \
         --region us-east-1 \
         --vehicle-name fwdev-custom-functions \
         --node-file can-nodes.json \
         --decoder-file can-decoders.json \
         --network-interface-file network-interface-can.json \
         --campaign-file campaign-math.json \
         --data-destination IOT_TOPIC
      ```

   1. To evaluate the `MULTI_RISING_EDGE_TRIGGER` custom function, run this command. The campaign
      will be triggered when either the signal `Vehicle.ECM.DemoEngineTorque` is greater than 500
      (`ALARM1`) or signal `Vehicle.ABS.DemoBrakePedalPressure` is greater than 7000 (`ALARM2`). See
      [`campaign-multi-rising-edge-trigger.json`](../../tools/cloud/campaign-multi-rising-edge-trigger.json).

      ```bash
      ./demo.sh \
         --region us-east-1 \
         --vehicle-name fwdev-custom-functions \
         --node-file can-nodes.json \
         --decoder-file can-decoders.json \
         --network-interface-file network-interface-can.json \
         --node-file custom-nodes-multi-rising-edge-trigger.json \
         --decoder-file custom-decoders-multi-rising-edge-trigger.json \
         --network-interface-file network-interface-custom-named-signal.json \
         --campaign-file campaign-multi-rising-edge-trigger.json \
         --data-destination IOT_TOPIC
      ```

   1. Scripting examples - you must have
      [provisioned FWE with scripting support](#provision-fwe-with-scripting-support) above.

      1. To evaluate the Python custom function, run these commands to copy the example script
         [`histogram.py`](../../tools/cloud/custom-function-python-histogram/histogram.py) to the S3
         bucket and deploy the campaign. The campaign will calculate a histogram of the signal
         `Vehicle.ECM.DemoEngineTorque`, collecting the histogram data as a JSON serialized string
         for the signal `Vehicle.Histogram`. See
         [`campaign-python-histogram.json`](../../tools/cloud/campaign-python-histogram.json).

         ```bash
         source demo.env \
         && aws s3 cp \
             ../../tools/cloud/custom-function-python-histogram/histogram.py \
             s3://${S3_BUCKET_NAME}/custom-function-python-histogram/histogram.py \
         && ./demo.sh \
           --region us-east-1 \
           --vehicle-name fwdev-custom-functions \
           --node-file can-nodes.json \
           --decoder-file can-decoders.json \
           --network-interface-file network-interface-can.json \
           --node-file custom-nodes-histogram.json \
           --decoder-file custom-decoders-histogram.json \
           --network-interface-file network-interface-custom-named-signal.json \
           --campaign-file campaign-python-histogram.json \
           --data-destination IOT_TOPIC
         ```

1. When the `demo.sh` script completes, a path to an HTML file is given. _On your local machine_,
   use `scp` to download it, then open it in your web browser:

   ```bash
   scp -i <PATH_TO_PEM> ubuntu@<EC2_IP_ADDRESS>:<PATH_TO_HTML_FILE> .
   ```

### Clean up

1. Run the following _on the development machine_ to clean up resources created by the
   `provision.sh` and `demo.sh` scripts.

   ```bash
   cd ~/aws-iot-fleetwise-edge/tools/cloud \
   && ./clean-up.sh \
   && ../provision.sh \
      --vehicle-name fwdev-custom-functions \
      --region us-east-1 \
      --only-clean-up
   ```

1. Delete the CloudFormation stack for your development machine, which by default is called `fwdev`:
   https://us-east-1.console.aws.amazon.com/cloudformation/home

## Developing your own custom functions

To develop your own custom function in C++, you must write code to meet the
`CustomFunctionCallbacks` interfaces:

```cpp
using CustomFunctionInvokeCallback = std::function<CustomFunctionInvokeResult(
    CustomFunctionInvocationID invocationId, // Unique ID for each invocation
    const std::vector<InspectionValue> &args)>;
using CustomFunctionConditionEndCallback = std::function<void(
    const std::unordered_set<SignalID> &collectedSignalIds,
    Timestamp timestamp,
    CollectionInspectionEngineOutput &output)>;
using CustomFunctionCleanupCallback = std::function<void(
    CustomFunctionInvocationID invocationId)>;

struct CustomFunctionCallbacks {
    CustomFunctionInvokeCallback invokeCallback;
    CustomFunctionConditionEndCallback conditionEndCallback;
    CustomFunctionCleanupCallback cleanupCallback;
};
```

### Interface `invokeCallback`

At minimum, if your function does not need to save state information between calls, you only need to
define the `invokeCallback` and can set `conditionEndCallback` and `cleanupCallback` to `nullptr`.
For example, the following would implement a `sin` function to calculate the mathematical sine of
the argument:

```cpp
#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <cmath>
#include <vector>

Aws::IoTFleetWise::CustomFunctionInvokeResult
customFunctionSin( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
                   const std::vector<Aws::IoTFleetWise::InspectionValue> &args )
{
    static_cast<void>( invocationId );
    if ( args.size() != 1 )
    {
        return Aws::IoTFleetWise::ExpressionErrorCode::TYPE_MISMATCH;
    }
    if ( args[0].isUndefined() )
    {
        return Aws::IoTFleetWise::ExpressionErrorCode::SUCCESSFUL; // Undefined result
    }
    if ( !args[0].isBoolOrDouble() )
    {
        return Aws::IoTFleetWise::ExpressionErrorCode::TYPE_MISMATCH;
    }
    return { Aws::IoTFleetWise::ExpressionErrorCode::SUCCESSFUL, std::sin( args[0].asDouble() ) };
}
```

Then the custom function can be registered in your bootstrap code via
`IoTFleetWiseEngine::setStartupConfigHook` as follows:

```cpp
engine.mCollectionInspectionEngine->registerCustomFunction(
    "sin",
    { customFunctionSin, nullptr, nullptr }
);
```

### Interface `conditionEndCallback`

If you would like to add to the collected data when the campaign triggers data collection, you can
also implement the `conditionEndCallback` which is called after evaluation of each condition. To do
this you will need to know the signal ID to add the data, so you will probably want to use the
`NamedSignalDataSource` to lookup the signal ID for a given fully-qualified-name. For example, if
you would like to implement a function that returns the file size of a given filename, and adds this
to the collected data with the signal name `Vehicle.FileSize`:

```cpp
#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <aws/iotfleetwise/LoggingModule.h>
#include <aws/iotfleetwise/NamedSignalDataSource.h>
#include <aws/iotfleetwise/SignalTypes.h>
#include <aws/iotfleetwise/TimeTypes.h>
#include <boost/filesystem.hpp>
#include <exception>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

class CustomFunctionFileSize
{
public:
    CustomFunctionFileSize(
        std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> namedSignalDataSource )
        : mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    {
        if ( mNamedSignalDataSource == nullptr )
        {
            throw std::runtime_error( "namedSignalInterface is not configured" );
        }
    }
    Aws::IoTFleetWise::CustomFunctionInvokeResult
    invoke( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
            const std::vector<Aws::IoTFleetWise::InspectionValue> &args )
    {
        static_cast<void>( invocationId );
        if ( ( args.size() != 1 ) || ( !args[0].isString() ) )
        {
            return Aws::IoTFleetWise::ExpressionErrorCode::TYPE_MISMATCH;
        }
        try
        {
            boost::filesystem::path filePath( *args[0].stringVal );
            mFileSize = static_cast<int>( boost::filesystem::file_size( filePath ) );
        }
        catch ( const std::exception &e )
        {
            return Aws::IoTFleetWise::ExpressionErrorCode::TYPE_MISMATCH;
        }
        return { Aws::IoTFleetWise::ExpressionErrorCode::SUCCESSFUL, mFileSize };
    }
    void
    conditionEnd( const std::unordered_set<Aws::IoTFleetWise::SignalID> &collectedSignalIds,
                  Aws::IoTFleetWise::Timestamp timestamp,
                  Aws::IoTFleetWise::CollectionInspectionEngineOutput &output )
    {
        // Only add to the collected data if we have a valid value:
        if ( mFileSize < 0 )
        {
            return;
        }
        // Clear the current value:
        auto size = mFileSize;
        mFileSize = -1;
        // Only add to the collected data if collection was triggered:
        if ( !output.triggeredCollectionSchemeData )
        {
            return;
        }
        auto signalId = mNamedSignalDataSource->getNamedSignalID( "Vehicle.FileSize" );
        if ( signalId == Aws::IoTFleetWise::INVALID_SIGNAL_ID )
        {
            FWE_LOG_WARN( "Vehicle.FileSize not present in decoder manifest" );
            return;
        }
        if ( collectedSignalIds.find( signalId ) == collectedSignalIds.end() )
        {
            return;
        }
        output.triggeredCollectionSchemeData->signals.emplace_back(
            Aws::IoTFleetWise::CollectedSignal{ signalId, timestamp, size, Aws::IoTFleetWise::SignalType::DOUBLE } );
    }

private:
    std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> mNamedSignalDataSource;
    int mFileSize{ -1 };
};
```

Then the custom function can be registered in your bootstrap code via
`IoTFleetWiseEngine::setStartupConfigHook` as follows:

```cpp
auto fileSizeFunc = std::make_shared<CustomFunctionFileSize>( engine.mNamedSignalDataSource );
engine.mCollectionInspectionEngine->registerCustomFunction(
    "file_size",
    {
        [fileSizeFunc]( auto invocationId, const auto &args ) {
            return fileSizeFunc->invoke( invocationId, args );
        },
        [fileSizeFunc]( const auto &collectedSignalIds, auto timestamp, auto &collectedData ) {
            fileSizeFunc->conditionEnd( collectedSignalIds, timestamp, collectedData );
        },
        nullptr
    }
);
```

### Interface `cleanupCallback`

Lastly if you would like to store some state information in between calls to the custom function,
you should use the `invocationId` argument to the `invokeCallback` to uniquely identify each
invocation of the function, and implement the `cleanupCallback` to cleanup the old state information
at the end of the lifetime of the function. The `invocationId` will be the same each time the
function is called for the lifetime of the campaign. For example, if you would like to implement a
custom function that returns a counter that is incremented each time the function is called:

```cpp
#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <unordered_map>
#include <vector>

class CustomFunctionCounter
{
public:
    Aws::IoTFleetWise::CustomFunctionInvokeResult
    invoke( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
            const std::vector<Aws::IoTFleetWise::InspectionValue> &args )
    {
        static_cast<void>( args );
        // Create a new counter if the invocationId is new, or get the existing counter:
        auto &counter = mCounters.emplace( invocationId, 0 ).first->second;
        return { Aws::IoTFleetWise::ExpressionErrorCode::SUCCESSFUL, counter++ };
    }
    void
    cleanup( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId )
    {
        mCounters.erase( invocationId );
    }

private:
    std::unordered_map<Aws::IoTFleetWise::CustomFunctionInvocationID, int> mCounters;
};
```

Then the custom function can be registered in your bootstrap code via
`IoTFleetWiseEngine::setStartupConfigHook` as follows:

```cpp
auto counterFunc = std::make_shared<CustomFunctionCounter>();
engine.mCollectionInspectionEngine->registerCustomFunction(
    "counter",
    {
        [counterFunc]( auto invocationId, const auto &args ) {
            return counterFunc->invoke( invocationId, args );
        },
        nullptr,
        [counterFunc]( auto invocationId ) {
            counterFunc->cleanup( invocationId );
        }
    }
);
```

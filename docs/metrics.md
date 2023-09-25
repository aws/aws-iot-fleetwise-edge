# Application level metrics

The Reference Implementation for AWS IoT Fleetwise ("FWE") includes a
[TraceModule](../src/TraceModule.cpp). The TraceModule provides a set of metrics that are used as an
entry point to efficiently diagnose issues, saving you time since you no longer need to review the
entire log of all FWE instances running.

- **`RFrames0` - `RFrames19`** are monotonic counters of the number of raw can frames read on each
  bus. If these counters remain null or remain fixed for a longer runtime, the system might either
  have no CAN bus traffic or there is no CAN bound data collection campaign ( e.g. OBD2 only
  campaign ).
- **`ConInt`** and **`ConRes`** enable you to monitor the the number of MQTT connection
  interruptions and connection resumptions. If and how long it takes to detect a connection loss
  depends on the kernel configuration parameters `/proc/sys/net/ipv4/tcp/keepalive*` and the compile
  time constants of FWE: `MQTT_CONNECT_KEEP_ALIVE_SECONDS` and `MQTT_PING_TIMOUT_MS`. If the values
  of the metric `ConInt` are not null, the internet coverage in the tested environment might be
  unreliable, or `MQTT_PING_TIMOUT_MS`, which defaults to 3 seconds, needs to be increased because
  there's high latency to the IoT Core endpoint. Changing the AWS Region can help to decrease
  latency.
- **`CeTrgCnt`** is a monotonic counter that monitors the number of triggers (inspection rules)
  detected since the FWE process started. Triggers are detected if one or more data collection
  campaign conditions are true. If this counter is larger than zero, but no data appears in the
  cloud, either no actual data was collected ( such as a time-based data collection campaign with no
  bus activity), or the data has been ingested to the cloud but there was an error processing it. To
  debug this,
  [enable cloud logs in AWS IoT Fleetwise settings](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/logging-cw.html).
- **`QUEUE_CONSUMER_TO_INSPECTION_SIGNALS`** monitors the current count of signals in queue to the
  signal history buffer. If this value is close to the value defined in the static config
  `decodedSignalsBufferSize`, increase the static config, decrease `inspectionThreadIdleTimeMs`,
  reduce the bus load or reduce the amount of decoded signals in the decoder manifest in the cloud.
- **`ConRej`** monitors the number of MQTT connection rejects. If this is not zero check the
  certificates and make sure you use a unique client id for each vehicle.
- **`ConFail`** monitors the number of MQTT connection failures. This can have multiple root causes.
  If this is not zero please check the logs and search for `Connection failed with error`
- **`FWE_STARTUP`** and **`FWE_SHUTDOWN`** provide the amount of time it takes to start and stop the
  AWS IoT Edge Fleetwise process. If any value is more than 5 seconds, review the logs and make sure
  all required resources such as internet and buses are available before starting the process.
- **`ObdE0`** to **`ObdE3`** monitors errors related to the OBD session. If you see non-zero values,
  make sure you're connected to a compatible OBD vehicle which is powered on. Otherwise turn off the
  OBD signals collection in the cloud.
- **`PmE3`** provides hints on whether the data persistency framework (a mechanism used to store and
  forward vehicle data when no connectivity is available) has an error. If this error counter is not
  zero, make sure that the directory defined in `persistencyPath` is writeable and that there is
  space available in the filesystem
- **`SysKerTimeDiff`** shows the difference between the CAN frame RX timestamp from the kernel and
  the system time. If this is significantly higher than `socketCANThreadIdleTimeMs`, which is 50
  milliseconds in the default configuration, the timestamps from the kernel are out of sync. Make
  sure an updated SocketCAN driver for your CAN device is used. Alternatively, consider switching
  `timestampType` in the static config to `Polling`. This will affect timestamp precision. Consider
  reducing the polling time `socketCANThreadIdleTimeMs` to mitigate.
- `CeSCnt` is a monotonic counter that counts the signals decoded and processed since startup. This
  can be used for performance evaluations.
- `CpuPercentageSum` and `CpuThread_*` tracks the CPU usage for the complete process and per thread.
  In multi-core systems this can be above 100%. FWE uses the linux `/proc/` directory to calculate
  this information.
- `MemoryMaxResidentRam` gives the maximum bytes of resident RAM used by the process. If this is
  above 50 MB high consider switching from cmake Debug to Release build. Also the queue sizes in the
  static config can be reduced.
- `CampaignFailures` monitors errors related to the campaign activation. If you see non-zero values,
  please check the logs. Make sure not to deploy more campaigns in parallel than defined in
  `MAX_NUMBER_OF_ACTIVE_CONDITION`, which defaults to 256. Also check that the `maxSampleCount` of
  all collected signals fits into the memory used for the signals history buffer defined in
  `MAX_SAMPLE_MEMORY`, which defaults to 20MB.
- `CampaignRxToDataTx` provides the amount of time it takes from changing the set of active
  campaigns to the first signal data being published. If at least one time based collection scheme
  is active this should be at most the time period of that collection scheme.

# How to collect metrics from FWE

There are multiple ways to collect metrics depending on how FWE is integrated. We describe two
methods: using the RemoteProfiler and collecting processed logs and extract metrics (like through
the AWS Systems Manager).

Each method incurs charges for different AWS services like
[AWS IoT Core](https://aws.amazon.com/iot-core/pricing/),
[Amazon CloudWatch](https://aws.amazon.com/cloudwatch/pricing/),
[AWS System Manager](https://aws.amazon.com/systems-manager/pricing/) and more. For example, using
the [RemoteProfiler](#method-1-use-the-remoteprofiler-module) method, FWE uploads at your configured
interval, which is currently ~300 metrics. Per 10 metrics data points uploaded, at least one message
will be published to AWS IoT Core and one AWS IoT Rules Engine Action will be executed. If
`profilerPrefix` is different for every vehicle, ~300 new Amazon CloudWatch metrics will be used per
vehicle.

## Method 1: Use the RemoteProfiler module

The RemoteProfiler module is provided as part of the FWE C++ code base. If activated, it will
regularly ingest the metrics and logs to AWS IoT Core topics, which have underlying AWS IoT Core
Rules and actions to route the data to Amazon CloudWatch. The same existing MQTT connection used to
ingest the data collection campaign is reused for this purpose. In order to activate the
RemoteProfile, add the following parameters to your config file:

```json
{
    ...
    "staticConfig": {
        ...
        "mqttConnection": {
            ...
            "metricsUploadTopic": "aws-iot-fleetwise-metrics-upload",
            "loggingUploadTopic": "aws-iot-fleetwise-logging-upload"
        },
        "remoteProfilerDefaultValues": {
            "loggingUploadLevelThreshold": "Warning",
            "metricsUploadIntervalMs": 60000,
            "loggingUploadMaxWaitBeforeUploadMs": 60000,
            "profilerPrefix": "TestVehicle1"
        },
    }
}
```

In the above example configuration, a plain text json file with metrics will be uploaded to the AWS
IoT Core topic: `aws-iot-fleetwise-metrics-upload` and log messages of level Warning and Error to
the topic `aws-iot-fleetwise-logging-upload`. If `profilerPrefix` is unique for every vehicle, such
as if it's the same as `clientId`, there will be separate Amazon CloudWatch metrics for each
vehicle. If all vehicles have the same `profilerPrefix`, Amazon CloudWatch metrics are aggregated.

Two AWS IoT Core rule actions are needed for these topics to forward the data to Amazon CloudWatch
metrics and logs. They can be created by using the following AWS CloudFormation stack template:
[fwremoteprofiler.yml](../tools/cfn-templates/fwremoteprofiler.yml)

Click here to
[**Launch CloudFormation Template**](https://us-east-1.console.aws.amazon.com/cloudformation/home?region=us-east-1#/stacks/quickcreate?templateUrl=https%3A%2F%2Faws-iot-fleetwise.s3.us-west-2.amazonaws.com%2Flatest%2Fcfn-templates%2Ffwremoteprofiler.yml&stackName=fwremoteprofiler).

After the first vehicle uploads metrics, they can be found under the namespace
**AWSIotFleetWiseEdge**. The format is
`{profilerPrefix}_(variableMaxSinceStartup|variableMaxSinceLast|)_{name}` for variables and
`{profilerPrefix}_(sectionAvgSinceStartup|sectionCountSinceStartup|sectionMaxSinceLast|sectionMaxSinceStartup)_{name}`
for measuring the time in seconds needed for certain code sections. After running a vehicle with the
above config the metrics `TestVehicle1_variableMaxSinceStartup_RFrames0` and ~ 300 more will appear
in Amazon CloudWatch. Every minute new values will appear as `metricsUploadIntervalMs` is set
to 60000.

For the direct upload of every log message above the specified threshold
(`loggingUploadLevelThreshold`), log messages are cached at edge for a maximum of 60 seconds
(`loggingUploadMaxWaitBeforeUploadMs`) before being uploaded over MQTT.

The RemoteProfile module will not cache any metrics or logs during the loss of connectivity. The
local system log file can be used in that case, see the following section.

## Method 2: Retrieving metrics from logs e.g. over SSH

This method uses remote access, such as over SSH leveraging AWS Systems Manager or AWS IoT secure
tunneling to access the logs/metrics. In our examples, we use journald to manage the FWE logs. This
has the benefits of log rotation which might be necessary as FWE logs on TRACE level under high load
can produce multiple gigabytes of logs per day. These logs can be collected fully or aggregated like
over ssh from single vehicles in case of need for debugging or cyclically from the whole fleet. To
manage easy remote connections to multiple vehicles AWS Systems Manager or AWS IoT secure tunneling
could be used. For aggregation, custom scripts can be used to filter certain log levels. The log
levels in the FWE logs go from `[ERROR]` to `[TRACE]`. To make the metrics easier to parse, you can
set the parameter `.staticConfig.internalParameters.metricsCyclicPrintIntervalMs` in the static
config an interval like 60000. This will cause the metrics to print in an easy parsable format to
the log every minute. The following regex expression can be used by any log/metrics
aggregator/uploader that supports Python. For lines that start with
`TraceModule-ConsoleLogging-TraceAtomicVariable` or `TraceModule-ConsoleLogging-Variable`:

```python
regex_variable = re.compile(
    r".*\'(?P<name>.*?)\'"
    r" \[(?P<id>.*?)\]"
    r" .*\[(?P<current>.*?)\]"
    r" .*\[(?P<temp_max>.*?)\]"
    r" .*\[(?P<max>.*?)\]"
)
```

For lines starting with `TraceModule-ConsoleLogging-Section`

```python
regex_section = re.compile(
    r".*\'(?P<name>.*?)\'"
    r" \[(?P<id>.*?)\]"
    r" .*\[(?P<times>.*?)\]"
    r" .*\[(?P<avg_time>.*?)\]"
    r" .*\[(?P<tmp_max_time>.*?)\]"
    r" .*\[(?P<max_time>.*?)\]"
    r" .*\[(?P<avg_interval>.*?)\]"
    r" .*\[(?P<tmp_max_interval>.*?)\]"
    r" .*\[(?P<max_interval>.*?)\]"
)
```

After the metrics are parsed from the local log files, a local health monitoring program can decide
if and how to upload them to the cloud.

# Adding new metrics

Adding new metrics requires changing the C++ code and recompiling FWE. Add the metrics to the
`TraceVariable` enum in [TraceModule.h](../src/TraceModule.h) and assign a short name in the
function `getVariableName` of [TraceModule.cpp](../src/TraceModule.cpp). Then you can set the
metrics anywhere by using:

```cpp
 TraceModule::get().setVariable( TraceVariable::MAX_SYSTEMTIME_KERNELTIME_DIFF, observedNewValue);
```

The metric will be automatically included in both methods described above. There are no changes
needed in the cloud, and the new metric will just show up in the same namespace.

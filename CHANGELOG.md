# Change Log

## v1.3.0 (2025-04-14)

Bug fixes:

- Fixed crash when using Greengrass V2 connection type.

Improvements:

- Improved [guide for Greengrass V2](./tools/greengrassV2/README.md)
- Added an option to [fwdemo stack](./tools/cfn-templates/fwdemo.yml) to use Greengrass V2
  connection type. When `MqttConnectionType` is set to `iotGreengrassV2`, the EC2 instance will be
  configured as a Greengrass core device and FWE will be deployed as a Greengrass component.
- Make SHA1 code compatible with Boost `1.86.0`. The `1.84.0` version is still being used as a
  dependency, but the code should now compile with more recent versions too. Related to
  https://github.com/aws/aws-iot-fleetwise-edge/issues/119
- Added [system test framework](./test/system/) with 70 reference system tests
- Update to Ubuntu 22.04 for the development environment
  - Upgraded the ROS2 distribution from Galactic to Humble

## v1.2.1 (2025-02-27)

New features:

- Added Python support in expressions with
  [custom function](./docs/dev-guide/custom-function-dev-guide.md#python-custom-function)
  implementation for both [CPython](https://github.com/python/cpython) and
  [MicroPython](https://github.com/micropython/micropython).
- Added support for IEEE 754 floating-point signals for CAN and OBD.
- Added support for signed OBD PID signals.
- Added support for using FWE as an external library, see the [examples](./examples/README.md).

Bug fixes:

- Fixed deletion of buffer after hand over to sender.
- Fixed memory leak when generating Ion files for Vision System Data. This was caused by
  [a known issue in the ion-c library](https://github.com/amazon-ion/ion-c/issues/264). To avoid
  that all `ion-c` function calls now happen in the same thread.
- Fixed possible thread lockups in rare cases when system time jumps to the future, or due to a
  [`stdlibc++` issue](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=41861). Includes the addition of
  the new option `collectionSchemeManagerThreadIdleTimeMs`.
- Fixed sporadic SOME/IP build failures due to concurrent file generation.
- Fixed possible hang during shutdown when feature `FWE_FEATURE_UDS_DTC_EXAMPLE` was enabled.

Improvements:

- Add checksum for persisted files (e.g Collection scheme, Decoder manifest, Telemetry data). Now
  when writing a file, the persistency layer calculates the SHA1 for the content and write it to a
  file alongside the content file with a `.sha1` extension. When reading the file, if the `.sha1`
  file doesn't exist it just logs a warning. This is intended to keep backward compatibility with
  files that were written by older FWE versions. Otherwise if the `.sha1` file exists, a mismatch in
  the SHA1 when reading the content will cause both the content file and the `.sha1` file to be
  deleted.
- Store and forward optimized for systems with slow write speed to persistent storage.
- Update `GoogleTest` to `1.15.2`.
- Removed unsupported raw CAN frame collection.
- Added optional `awsSdkLogLevel` field to the config file. Valid values are `Off`, `Fatal`,
  `Error`, `Warn`, `Info`, `Debug`, `Trace`. Previously the AWS SDK logs were always disabled. This
  allows the logs to be configured in [AwsBootstrap.cpp](./src/AwsBootstrap.cpp) (see the
  [SDK docs](https://docs.aws.amazon.com/sdk-for-cpp/v1/developer-guide/logging.html) for more
  details). If this is set to some level different than `Off`, the SDK logs will be redirected to
  FWE's logger. Since FWE doesn't provide all levels, `Fatal` is mapped to FWE's `Error` and `Debug`
  to FWE's `Trace`.
- IMDS is now disabled when creating an S3 client. In some situations this could cause delays when
  creating a new client (for more details see https://github.com/aws/aws-sdk-cpp/issues/1511).

## v1.2.0 (2024-11-21)

New features:

- Remote commands for actuators, see the
  [CAN actuators guide](./docs/dev-guide/can-actuators-dev-guide.md) and the
  [SOME/IP guide](./docs/dev-guide/edge-agent-dev-guide-someip.md).
- [Network agnostic data collection and actuator commands](./docs/dev-guide/network-agnostic-dev-guide.md).
- SOME/IP support, see the
  [SOME/IP guide for data collection and commands](./docs/dev-guide/edge-agent-dev-guide-someip.md),
  the
  [Device shadow proxy for SOME/IP guide](./docs/dev-guide/edge-agent-dev-guide-device-shadow-over-someip.md),
  and the [CAN over SOME/IP guide](./docs/dev-guide/can-over-someip-demo.md).
- String datatype support for both sensor data collection and actuator commands.
- [Last Known State (LKS)](./docs/dev-guide/edge-agent-dev-guide-last-known-state.md), a lighter
  method of data collection.
- [Custom functions in expressions](./docs/dev-guide/custom-function-dev-guide.md).
- [Store and forward](./docs/dev-guide/store-and-forward-dev-guide.md), for conditional upload of
  collected data.
- [UDS DTC data collection](./docs/dev-guide/edge-agent-uds-dtc-dev-guide.md).

Breaking changes:

- The `collectionSchemeListTopic`, `decoderManifestTopic`, `canDataTopic` and `checkinTopic` config
  fields are deprecated. If a config file has any of them, they will be ignored. Now FWE defaults to
  AWS reserved topics without needing any additional config. If you still need to customize the
  topics you can set the prefix using the new `iotFleetWiseTopicPrefix` field.
- **Only affects existing iWave and Android instances**: Replaced the CAN-based `CustomDataSource`
  implementation with the new
  [Network agnostic data collection](./docs/dev-guide/network-agnostic-dev-guide.md) approach for
  custom data sources. Existing iWave and Android instances will need to be re-configured and have
  changes made to their decoder manifest to use this new version.
  - For iWave devices, see the [iWave guide](./docs/iwave-g26-tutorial/iwave-g26-tutorial.md).
  - For Android (including AAOS), see the [Android guide](./tools/android-app/README.md).

## v1.1.2 (2024-10-29)

Bug fixes:

- MQTT connection fixes:
  - Retry MQTT topic subscription when it fails. When FWE starts up, it tries to establish the MQTT
    connection and retries until it succeeds. Only after that, it subscribes to the topics. But if a
    subscription failed (e.g. due to network issues), it never retried, making FWE never receive
    messages from the topic until the process is restarted.
  - On shutdown only unsubscribe to MQTT Channels that are subscribed
  - When persistent sessions are enabled, don't unsubscribe on shutdown. This is required for FWE to
    receive messages while it was offline.
  - Connect only after all listeners are subscribed. If a message was received right after
    connection was established, it could be silently ignored because the listeners for the topic
    weren't registered yet.
- ROS2 related bug fixes:
  - If more than one campaign used a ROS2 signal in its expression, only the first campaign would
    receive data for evaluation.
  - If FWE receives a campaign more than once from the cloud, data collection would stop due to
    internal signal IDs being reallocated.
  - Fixed segfault for VSD build when campaigns received before decoder manifest
- Fixed invalid read on shutdown when network is down

Improvements:

- Update AWS C++ SDK to `v1.11.284`.
- Update Boost to `1.84.0`.
- Enable flow control for MQTT5 client (when using `iotCore` connection type). This limits data sent
  by FWE to what is defined in IoT Core limits for throughput and number of publishes.
- Change default values for some MQTT connection settings and make them configurable in the config
  file. All defaults are now the same as the AWS SDK:
  - Keep alive interval changed from `60` seconds to `1200` seconds. Set `keepAliveIntervalSeconds`
    to override it.
  - Ping timeout default changed from `3000` ms to `30000` ms. Set `pingTimeoutMs` to override it.
  - Persistent sessions are now disabled by default. Set `sessionExpiryIntervalSeconds` to a
    non-zero value to enable it.
- Add implicit casting between numeric and Boolean data types in expression evaluation. E.g.
  `1 + true` will equal `2`, and `false || 3` will equal `true`.
- Add support for relative paths to the certificate, private key files and persistency directory,
  relative to the directory containing the configuration JSON file.
- Make `demo.sh` more generic, by 1/ Allowing multiple 'node', 'decoder', 'network interface', and
  'campaign' JSON files to be passed, rather than having specific options for CAN, OBD and ROS2, 2/
  Add `--data-destination` option to specify data destination (default is still Amazon Timestream.)
  This allows the Android-specific demo script to be removed.
- Previously if the CAN network interface goes down, FWE would exit with an error. Now it will
  continue to run and will resume data collection if the interface comes back up, however it will
  still exit with an error if the interface is removed from the system.
- Improved developer guides
- Added Disconnect Packet to the MQTT Client Stop sequence to help customers understand what the
  reason for the vehicle disconnection from the broker
- Added usage of data type from decoder manifest for CAN and OBD signals
- Add new signal type UNKNOWN
- Split MQTT channels into separate Sender and Receiver compared to previously using the same
  channel instance to send and receive messages
- Adaptive payload sizing for both compressed and uncompressed data

## v1.1.1 (2024-02-12)

Bug fixes:

- Fixed possible segfaults at startup triggered by bad configuration.
- Fixed periodic upload trigger for heartbeat campaigns, that previously was only triggered by
  further data reception.

Improvements:

- Improve error output for bad configuration, to indicate where an option is missing or incorrect.
- Upgraded GitHub actions to support Node v20, as Node v16 is now EOL.
- Fixed some Coverity check regressions.
- Removed unsupported 'Geohash' feature.
- Corrected cleanup instructions in guides.
- Build Boost from source with `-fPIC` to enable linkage in a shared library.

## v1.1.0 (2023-11-26)

Features:

- Add support for 'vision system data', with initial support for collection from
  [ROS2](https://github.com/ros2). This enables collection of complex data types including
  structures, arrays and strings. Nested structures and arrays of structures are also supported.
  - **Known limitations:**
    - When no internet connection is available, collected vision system data is currently dropped,
      i.e. it is not persisted to the filesystem for later upload when connectivity is restored.
    - When the upload of vision system data to S3 fails, e.g. due to poor connectivity or throttling
      by S3, currently only one retry is attempted.

Improvements:

- Update AWS C++ SDK to v1.11.177.
- Update Yocto reference to kirkstone and NXP Linux BSP 37.0.
- Switch to MQTT 5 client for better error messages. This is fully backward compatible with the
  previous client. Please note that currently we are not using nor supporting any MQTT 5 specific
  feature besides reason codes.
- When a CAN interface goes down at runtime, FWE will now exit with with an error.
- Enabled `FWE_FEATURE_IWAVE_GPS` for the GitHub `armhf` pre-built-binary, and added auto-detection
  of the iWave GPS for backwards compatibility with configuration files without the
  `.staticConfig.iWaveGpsExample` section.

## v1.0.8 (2023-09-25)

Bug fixes:

- Update AWS C++ SDK to v1.11.148, which includes an
  [important bugfix](https://github.com/awslabs/aws-c-mqtt/pull/311) for MQTT 3.1.1 clients.

Improvements:

- Add support for building as a library exported by CMake. Set the CMake option
  `FWE_BUILD_EXECUTABLE` to `OFF`, then use `find_package(AwsIotFwe)`, `${AwsIotFwe_INCLUDE_DIR}`
  and link with `AwsIotFwe::aws-iot-fleetwise-edge`.
- Non-functional source code improvements:
  - Simplify `src/` folder structure, removing sub-namespaces & sub-libraries, and moving unit test
    files to `test/unit/`.
  - Correct `#include`s using
    [`include-what-you-use`](https://github.com/include-what-you-use/include-what-you-use).
  - Move from compile-time mocking to link-time mocking of AWS C++ SDK using Google Mock.
- Fix GitHub CI: support separate Ubuntu package mirror file, fix caching of Android install files.
- Add support for shared libraries to dependency install scripts.
- Developer guide and demo script improvements:
  - Add clean up instructions.
  - Better support existing S3 buckets, with check for same region & ACLs being disabled, and allow
    setting of bucket policy.
  - Allow HTML generation of results for custom DBC files.

## v1.0.7 (2023-08-01)

Features:

- Add Android Automotive (AAOS) support.
- Add experimental Greengrass V2 support

Bug fixes:

- Fix always saving data to disk when offline, even when configured not to in the campaign.
- Fix possible NullPointerException in Android app.

Improvements:

- Refactor persistent file handling, which now saves files under a subfolder called
  `FWE_Persistency` in the directory configured in the config file by `persistencyPath`. Now a
  separate file is saved for each payload file to be uploaded. An extra file `PayloadMetadata.json`
  is created containing metadata for these files, the schema for which can be found
  [here](./interfaces/persistency/schemas/persistencyMetadataFormat.json).
- Reduce Android app `minSdk` to 21 (Android 5.0).
- Update AWS C++ SDK to v1.11.111.

Deprecation:

- Remove the experimental camera feature (`-DFWE_FEATURE_CAMERA`). This is unsupported and it was
  not being maintained.

## v1.0.6 (2023-06-12)

Features:

- Add Android support, including shared library and demonstration app.

Improvements:

- Change from `arn` to `sync_id` for campaign_arn and document_arns, the `sync_id` being the ARN
  followed by the timestamp of the last update. The change is backwards compatible with older
  versions of FWE.
- Ubuntu package mirror from system used, rather than `ports.ubuntu.com`.
- Add root CA and inline credentials support to static config file.
- Add extra metrics for AWS SDK heap usage, used signal buffer, MQTT messages sent out.
- Add support for in-process ingestion of external GPS, CAN and OBD PID values when FWE is compiled
  as a shared library.
- Fix compiler warnings for armhf build.
- Update Protobuf to v3.21.12, AWS C++ SDK to v1.11.94.

## v1.0.5 (2023-05-11)

Bugfixes:

- RemoteProfiler not always uploading logs

Improvements:

- Refactor Producer/Consumer architecture, removing the buffer and thread between the
  `CANDataSource` and the `CANDataConsumer`. The static config option `socketCANBufferSize` was
  therefore removed.
- Add documentation on [how to use edge specific metrics](docs/metrics.md).
- Change from `arn` to `sync_id` for all decoder manifest Protobuf fields, the `sync_id` being the
  ARN followed by the timestamp of the last update. The change is backwards compatible with older
  versions of FWE.
- Improve MISRA C++ 2008, and AUTOSAR C++ compliance.
- Updated CloudFormation templates to use
  [IMDSv2](https://aws.amazon.com/blogs/security/defense-in-depth-open-firewalls-reverse-proxies-ssrf-vulnerabilities-ec2-instance-metadata-service/).

## v1.0.4 (2023-03-02)

Bugfixes:

- Fix OBD timers not being reset. If the decoder manifest is empty or DTCs are not collected the OBD
  PID or DTC timers were not reset, causing a 100% CPU and log spam with the following message
  `[WARN ] [OBDOverCANModule::doWork]: [Request time overdue by -X ms]`.
- Support OBD2 PIDs ECU response in different order than requested. Also accept ECU response if not
  all requested PIDs are answered.
- Unsubscribe and disconnect from MQTT on shutdown: previously a message arriving during shutdown
  could cause a segmentation fault.

Improvements:

- Update to Ubuntu 20.04 for the development environment.
- Add flake8 checking of Python scripts.
- Improve GitHub CI caching.
- Improve MISRA C++ 2008, and AUTOSAR C++ compliance.
- Improve logging: macros used to automatically add file, line number and function.
- Improve unit test stability, by replacing sleep statements with 'wait until' loops.
- Removed redundant JSON output code from `DataCollection*` files.

Work in Progress:

- Support for signal datatypes other than `double`, including `uint64_t` and `int64_t`.

## v1.0.3 (2023-01-09)

Features:

- Added OBD broadcast support to send functional rather than physical requests to ECUs to improve
  compatibility with a broader range of vehicles. This behavior does however increase CAN bus load.
  The config option `broadcastRequests` can be set to `false` to disable it.

Bugfixes:

- Fix `CollectionSchemeManager` and `CollectionInspectionEngine` to use monotonic clock This now
  makes check-in and data collection work even when the system time jumps. Please note that the
  timestamp present in check-in and collected data may still represent the system time, which means
  that newly collected data may be sent with a timestamp that is earlier than the previous sent data
  in case the system time is changed to some time in the past.

Improvements:

- Logs now show time in ISO 8601 format and UTC.
- Added optional config `logColor` for controlling ANSI colors in the logs. Valid values: `Auto`,
  `Yes`, `No`. Default value is `Auto`, which will make FWE try to detect whether stdout can
  interpret the ANSI color escape sequences.
- A containerized version of FWE is available from AWS ECR Public Gallery:
  https://gallery.ecr.aws/aws-iot-fleetwise-edge/aws-iot-fleetwise-edge.
- Improve CERT-CPP compliance.
- Improve quick start guide and demo script.
- Clarify the meaning of the `startBit`.

## v1.0.2 (2022-11-28)

Bugfixes:

- Fix `Timer` to use a monotonic clock instead of system time. This ensures the `Timer` will
  correctly measure the elapsed time when the system time changes.
- Use `std::condition_variable::wait_until` instead of `wait_for` to avoid the
  [bug](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=41861) when `wait_for` uses system time.
- Fix extended id not working with cloud.
- Handle `SIGTERM` signal. Now when stopping FWE with `systemctl` or `kill` without additional args,
  it should gracefully shutdown.
- Fix bug in canigen.py when signal offset is greater than zero.

Improvements:

- Pass `signalIDsToCollect` to `CANDecoder` by reference. This was causing unnecessary
  allocations/deallocations, impacting the performance on high load.
- Add binary distribution of executables and container images built using GitHub actions.
- Add support for DBC files with the same signal name in different CAN messages to cloud demo
  script.
- Improve CERT-CPP compliance.

## v1.0.1 (2022-11-03)

License Update:

- License changed from Amazon Software License 1.0 to Apache License Version 2.0

Security Updates:

- Update protcol buffer version used in customer build script to v3.21.7

Features:

- OBD module will automatic detect ECUs for both 11-bit and 29-bit. ECU address is no longer
  hardcoded.
- Support CAN-FD frames with up to 64 bytes
- Add an CustomDataSource for the IWave GPS module (NMEA output)
- iWave G26 TCU tutorial
- Renesas R-Car S4 setup guide

Bugfixes:

- Fix name of `persistencyUploadRetryIntervalMs` config. The dev guide wasn't including the `Ms`
  suffix and the code was mistakenly capitalizing the first letter.
- Don't use SocketCAN hardware timestamp as default but software timestamp. Hardware timestamp not
  being a unix epoch timestamp leads to problems.
- install-socketcan.sh checks now if can-isotp is already loaded.
- The not equal operator =! in expression is now working as expected
- Fix kernel timestamps in 32-bit systems

Improvements:

- Added Mac-user-friendly commands in quick demo
- Added an extra attribute, so that users can search vehicle in the FleetWise console
- Added two extra steps for quick demo: suspending campaigns and resuming campaigns

## v1.0.0 (2022-09-27)

Bugfixes:

- Fixed an OBD bug in which software requests more than six PID ranges in one message. The new
  revision request the extra range in a separate message.
- Fixed a bug in CANDataSource in which software didn't handle CAN message with extended ID
  correctly.

Improvements:

- Remove the HTML version of developer guide.
- Remove source code in S3 bucket. The S3 bucket will only be used to host quick demo
  CloudFormation.
- Remove `convertToPeculiarFloat` function from `DataCollectionProtoWriter`.
- Set default checkin period to 2-min in `static-config.json`. The quick demo will still use 5
  second as checkin period.
- Update FleetWise CLI Model to GA release version.
- Update Customer Demo to remove service-linked role creation for FleetWise Account Registration.

## v0.1.4 (2022-08-29)

Bugfixes:

- Fixed a bug in which software will continue requesting OBD-II PIDs or decoding CAN messages after
  all collection schemes removed.

Improvements:

- OBDOverCANModule will only request PIDs that are to be collected by Decoder Dictionary and
  supported by ECUs.
- OBDDataDecoder will validate the OBD PID response Length before decoding. If software detect
  response length mismatch with OBD Decoder Manifest, program will do 1) Log warning; 2) Discard the
  entire response.
- OBDDataDecoder will only decode the payload with the PIDs that previously requested.
- Improve OBD logging to log CAN ISOTP raw bytes for better debugging

## v0.1.3 (2022-08-03)

Customer Demo:

- Updated demo scripts to match with latest AWS IoT FleetWise Cloud
  [API changes](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/update-sdk-cli.html)
- Fix a bug in demo script that might render scatter plot incorrectly.

Docs:

- Updated the Edge Agent Developer Guide to match with latest AWS IoT FleetWise Cloud
  [API changes](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/update-sdk-cli.html)
- Updated Security Best Practices in Edge Agent Developer Guide

Bugfixes:

- Fixed a bug which previously prevented OBD from functioning at 29-bit mode.
- Fixed a bug that potentially caused a crash when two collection schemes were using the same Signal
  Ids in the condition with different minimum sampling intervals

Improvements:

- Signal Ids sent over Protobuf from the cloud can now be spread across the whole 32 bit range, not
  only 0-50.000
- Security improvement to pass certificate and private key by content rather than by file path
- Improvement to Google test CMake configuration
- Clang tidy coverage improvements
- Improvement to AWS SDK memory allocation with change to custom thread-safe allocator
- Re-organized code to remove cycles among CMake library targets
- Refactored Vehicle Network module to improve extensibility for other network types
- Improvement to cansim to better handle ISO-TP error.

## v0.1.2 (2022-02-24)

Features:

- No new features.

Bugfixes/Improvements:

- Unit tests added to release, including clang-format and clang-tidy tests.
- Source code now available on GitHub: https://github.com/aws/aws-iot-fleetwise-edge
  - GitHub CI job added that runs subset of unit tests that do not require SocketCAN.
- FWE source code:
  - No changes.
- Edge agent developer guide and associated scripts:
  - Cloud demo script `demo.sh`:
    - Fixed bug that caused the Timestream query to fail.
    - Script and files moved under edge source tree: `tools/cloud/`.
  - Dependency installation scripts:
    - AWS IoT C++ SDK updated to v1.14.1
    - Support for GitHub CI caching added.
  - CloudFormation template `fwdemo.yml` updated to pull source from GitHub instead of S3.
  - Developer guide converted to Markdown.

## v0.1.1 (2022-01-25)

Features:

- No new features.

Bugfixes/Improvements:

- FWE source code:
  - Fixed bug in `PayloadManager.cpp` that caused corruption of the persisted data.
  - Improved the documentation of the Protobuf schemas.
  - Added retry with exponential back-off for making initial connection to AWS IoT Core.
  - Added retry for uploading previously-collected persistent data.
- Edge agent developer guide and associated scripts:
  - Fixed bug in `install-socketcan.sh` that caused the `can-gw` kernel module not to be loaded,
    which prevented data from being generated when the fleet size was greater than one.
  - Edge agent developer guide now available in HTML format as well as PDF format.
  - Cloud demo script `demo.sh`:
    - Added retry loop if registration fails due to eventual-consistency of IAM.
    - Added `--force-registration` option to allow re-creation of Timestream database or service
      role, if these resources have been manually deleted.
    - Updated `iotfleetwise-2021-06-17.json` to current released version, which improves the
      parameter validation and help documentation.
  - CloudFormation templates `fwdemo.yml` and `fwdev.yml`:
    - Kernel updated and SocketCAN modules installed from `linux-modules-extra-aws` to avoid modules
      becoming unavailable after system upgrade of EC2 instance.
    - FWE now compiled and run on the same EC2 instance, rather than using CodePipeline.

## v0.1.0 (2021-11-29)

Features:

- Initial preview release

Bugfixes/Improvements:

- N/A

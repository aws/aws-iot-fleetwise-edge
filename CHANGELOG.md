# Change Log

## v1.0.8 (2023-09-25)

Bug fixes:

- Update AWS C++ SDK to v1.11.148, which includes an
  [important bugfix](https://github.com/awslabs/aws-c-mqtt/pull/311) for MQTT 3.1.1 clients.

Improvements:

- Add support for building as a library exported by CMake. Set the CMake option
  `FWE_BUILD_EXECUTABLE` to `OFF`, then use `find_package(AwsIotFwe)`, `${AwsIotFwe_INCLUDE_DIR}`
  and link with `AwsIotFwe::AwsIotFwe`.
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

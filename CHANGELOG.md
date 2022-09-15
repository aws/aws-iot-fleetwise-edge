# Change Log

## future release (TBD)
Bugfixes:
* Fixed an OBD bug in which software requests more than six PID ranges in one message. The new revision request the extra range in a separate message.

## v0.1.4 (Aug 29, 2022)
https://s3.console.aws.amazon.com/s3/object/aws-iot-fleetwise?prefix=v0.1.4/aws-iot-fleetwise-edge.zip

Bugfixes:
* Fixed a bug in which software will continue requesting OBD-II PIDs or decoding CAN messages after all collection schemes removed.

Improvements:
* OBDOverCANModule will only request PIDs that are to be collected by Decoder Dictionary and supported by ECUs.
* OBDDataDecoder will validate the OBD PID response Length before decoding. If software detect response length mismatch with OBD Decoder Manifest, program will do 1) Log warning; 2) Discard the entire response.
* OBDDataDecoder will only decode the payload with the PIDs that previously requested.
* Improve OBD logging to log CAN ISOTP raw bytes for better debugging

## v0.1.3 (Aug 3, 2022)
https://s3.console.aws.amazon.com/s3/object/aws-iot-fleetwise?prefix=v0.1.3/aws-iot-fleetwise-edge.zip

Customer Demo:
* Updated demo scripts to match with latest AWS IoT FleetWise Cloud [API changes](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/update-sdk-cli.html)
* Fix a bug in demo script that might render scatter plot incorrectly. 

Docs:
* Updated the Edge Agent Developer Guide to match with latest AWS IoT FleetWise Cloud [API changes](https://docs.aws.amazon.com/iot-fleetwise/latest/developerguide/update-sdk-cli.html)
* Updated Security Best Practices in Edge Agent Developer Guide

Bugfixes:
* Fixed a bug which previously prevented OBD from functioning at 29-bit mode.
* Fixed a bug that potentially caused a crash when two collection schemes were using the same
  Signal Ids in the condition with different minimum sampling intervals

Improvements:
* Signal Ids sent over Protobuf from the cloud can now be spread across the whole 32 bit range,
  not only 0-50.000
* Security improvement to pass certificate and private key by content rather than by file path
* Improvement to Google test CMake configuration
* Clang tidy coverage improvements
* Improvement to AWS SDK memory allocation with change to custom thread-safe allocator
* Re-organized code to remove cycles among CMake library targets
* Refactored Vehicle Network module to improve extensibility for other network types
* Improvement to cansim to better handle ISO-TP error. 

## v0.1.2 (February 24, 2022)
https://s3.console.aws.amazon.com/s3/object/aws-iot-fleetwise?prefix=v0.1.2/aws-iot-fleetwise-edge.zip

Features:
* No new features.

Bugfixes/Improvements:
* Unit tests added to release, including clang-format and clang-tidy tests.
* Source code now available on GitHub: https://github.com/aws/aws-iot-fleetwise-edge
  * GitHub CI job added that runs subset of unit tests that do not require SocketCAN.
* Edge agent source code:
  * No changes.
* Edge agent developer guide and associated scripts:
  * Cloud demo script `demo.sh`:
    * Fixed bug that caused the Timestream query to fail.
    * Script and files moved under edge source tree: `tools/cloud/`.
  * Dependency installation scripts:
    * AWS IoT C++ SDK updated to v1.14.1
    * Support for GitHub CI caching added.
  * CloudFormation template `fwdemo.yml` updated to pull source from GitHub instead of S3.
  * Developer guide converted to Markdown.

## v0.1.1 (January 25, 2022)
https://s3.console.aws.amazon.com/s3/object/aws-iot-fleetwise?prefix=v0.1.1/aws-iot-fleetwise-edge.zip

Features:
* No new features.

Bugfixes/Improvements:
* Edge agent source code:
  * Fixed bug in `PayloadManager.cpp` that caused corruption of the persisted data.
  * Improved the documentation of the Protobuf schemas.
  * Added retry with exponential back-off for making initial connection to AWS IoT Core.
  * Added retry for uploading previously-collected persistent data.
* Edge agent developer guide and associated scripts:
  * Fixed bug in `install-socketcan.sh` that caused the `can-gw` kernel module not to be loaded,
    which prevented data from being generated when the fleet size was greater than one.
  * Edge agent developer guide now available in HTML format as well as PDF format.
  * Cloud demo script `demo.sh`:
    * Added retry loop if registration fails due to eventual-consistency of IAM.
    * Added `--force-registration` option to allow re-creation of Timestream database or service
      role, if these resources have been manually deleted.
    * Updated `iotfleetwise-2021-06-17.json` to current released version, which improves the
      parameter validation and help documentation.
  * CloudFormation templates `fwdemo.yml` and `fwdev.yml`:
    * Kernel updated and SocketCAN modules installed from `linux-modules-extra-aws` to avoid
      modules becoming unavailable after system upgrade of EC2 instance.
    * Edge agent now compiled and run on the same EC2 instance, rather than using CodePipeline.

## v0.1.0 (November 29, 2021)
https://s3.console.aws.amazon.com/s3/object/aws-iot-fleetwise?prefix=v0.1.0/aws-iot-fleetwise-edge.zip

Features:
* Initial preview release

Bugfixes/Improvements:
* N/A

# Change Log

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

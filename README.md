AWS IoT FleetWise Edge
======================

> :information_source: To quickly get started, jump to the [Developer Guide](./docs/dev-guide/edge-agent-dev-guide.md)
  or the [Raspberry Pi Tutorial](./docs/rpi-tutorial/raspberry-pi-tutorial.md)

AWS IoT FleetWise is a service that makes it easy for Automotive OEMs to collect, store, organize, and monitor data from vehicles at scale. AWS IoT FleetWise Edge provides C++ libraries that allow you to run the application on your vehicle. You can then use AWS IoT FleetWise's pre-configured analytic capabilities to process the collected data, gain insights about the vehicle's health and use the service’s visual interface to help diagnose and troubleshoot potential issues with your vehicles. Furthermore, AWS IoT FleetWise's capability to collect ECU data and store them on cloud databases enables you to utilize different AWS services (Analytics Services, ML, etc.) to develop novel use-cases that augment your existing vehicle functionality. 

In particular, AWS IoT FleetWise can leverage fleet data (Big Data) and enable you to develop use cases that create business value, for example: improve electric vehicle range estimation, optimized battery life charging, optimized vehicle routing, etc. AWS IoT FleetWise can be extended to utilize cloud computing capabilities for use-cases such as pet/child detection, Driver Monitoring System applications, Predictive Diagnostics, electric vehicle's battery cells outlier detection, etc.

You can use the included sample C++ application to learn more about AWS IoT FleetWise Edge interfaces and to test interactions before integration.



## AWS IoT FleetWise Architecture

AWS IoT FleetWise is an AWS service that enables automakers to collect, store, organize, and monitor data from vehicles. Automakers need the ability to connect remotely to their fleet of vehicles and collect vehicle ECU/sensor data. AWS IoT FleetWise can be used by OEM engineers and data scientists to build vehicle models that can be used to build custom data collection schemes. These data collection schemes enables the OEM to optimize the data collection process by defining what signals to collect, how often to collect them, and most importantly the trigger conditions ("events") that enable the collection process.

Customers can define the data collection schemes to trigger based on a schedule or on specific conditions such as, but not limited to: 1. Ambient temperature dropping to below 0 degree or 2. Vehicle crosses state lines or 3. Active diagnostic trouble codes. These conditions are sent to the vehicle through a set of documents called data collection schemes. In summary, AWS IoT FleetWise Edge collects the data of interest according to the data collection schemes and decoding rules as specified by the OEM on the [AWS IoT FleetWise Console](https://aws.amazon.com/iot-fleetwise/).

The  following diagram illustrates a high-level architecture of the system.





<img src="./docs/iot-FleetWise-architecture.png" />



**AWS IoT FleetWise Edge receives two documents:**

1. *Decoder Manifest* - this document describes how signals are collected from the vehicle, and will include details such as, but not limited to: Bus ID, network name, decoding information, etc. 

2. *Data Collection Schemes* - this document describes what signals to collect. It also describes the condition logic that defines the enablement of the trigger logic that allows these signals to be collected, for example, when Vehicle Speed > 100 km/Hr and Driver Seatbelt is Off and Ambient Temperature <  0 degree C. 

## AWS IoT FleetWise Edge Deployment & Supported Platforms

AWS IoT FleetWise Edge functional flexibility and its use of dynamic memory allocation means that it cannot reside in the real-time safe vehicle ECUs. AWS IoT FleetWise Edge must also be connected to the internet and preferably has access to a “good” portion of vehicle ECU data. OEMs have the flexibility to decide  where they can deploy AWS IoT FleetWise Edge binary. Possible options include (if present):

1. Vehicle Gateway such as the [NXP S32G](https://www.nxp.com/products/processors-and-microcontrollers/arm-processors/s32g-vehicle-network-processors/s32g2-processors-for-vehicle-networking:S32G2) 
2. Vehicle Head-Unit
3. Vehicle’s High Performance Computer
4. Telecommunication Control Unit



AWS IoT FleetWise Edge was built and tested on 64-bit architectures. It has been tested on both ARM and X86 multicore based machines, with a Linux Kernel version of 5.4 and above. The kernel module for ISO-TP (can-isotp ) would need to be installed in addition for Kernels below 5.10.

AWS IoT FleetWise Edge was also tested on an EC2 Instance with the following details:

- **Platform**: Ubuntu
- **Platform Details**: Linux/UNIX
- **Server**: AmazonEC2
- **InstanceType**: c4.8xlarge
- **AvailabilityZone**: us-east-1
- **Architecture**: x86_64
- **CpuOptions**: {'CoreCount': 18, 'ThreadsPerCore': 2}
- **AMI name**: ubuntu-bionic-18.04-amd64-server-20210224



## AWS IoT FleetWise Client-Server Communication

AWS IoT FleetWise Edge relies on [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp) to send and receive data from and to AWS IoT FleetWise Server. All data sent to AWS IoT is sent over an encrypted [TLS connection](https://docs.aws.amazon.com/iot/latest/developerguide/data-encryption.html) using MQTT, HTTPS, and WebSocket protocols, making it secure by default while in transit. AWS IoT FleetWise uses MQTT quality of service zero (QoS = 0).



## Security

See [SECURITY](./SECURITY.md) for more information



## License Summary and Build Dependencies
AWS IoT FleetWise Edge depends on the following open source libraries. Refer to the corresponding links for more information.

* [AWS SDK for C++: v1.9.253](https://github.com/aws/aws-sdk-cpp)
* [cURL: v7.58.0](https://github.com/curl/curl)
* [GoogleTest version: release-1.10.0](https://github.com/google/googletest)
* [Benchmark version: 1.6.1](https://github.com/google/benchmark)
* [Protobuf version: 3.9.2](https://github.com/protocolbuffers/protobuf)
* [Boost version 1.65.1](https://github.com/boostorg/boost)
* [jsoncpp version 1.7.4](https://github.com/open-source-parsers/jsoncpp)
* [Snappy version: 1.1.7](https://github.com/google/snappy)

Optional: The following dependencies are only required when the experimental option `FWE_FEATURE_CAMERA` is enabled.

* [Fast-DDS version: 2.3.3](https://github.com/eProsima/Fast-DDS.git)
  * [Fast-CDR version: v1.0.21](https://github.com/eProsima/Fast-CDR.git)
  * [Foonathan memory vendor version: v1.1.0](https://github.com/eProsima/foonathan_memory_vendor.git)
  * [Foonathan memory version: v0.7](https://github.com/foonathan/memory)
  * [tinyxml2 version: 6.0.0](https://github.com/leethomason/tinyxml2.git)

See [LICENSE](./LICENSE) for more information.



## Getting Help

[Contact AWS Support](https://aws.amazon.com/contact-us/) if you have any technical questions about AWS IoT FleetWise Edge.



## Resources

The following documents provide more information about AWS IoT FleetWise Edge.

1. [Change Log](./CHANGELOG.md) provides a summary of feature enhancements, updates, and resolved and known issues.
2. [AWS IoT FleetWise Edge Offboarding](./docs/AWS-IoTFleetWiseOffboarding.md) provides a summary of the steps needed on the Client side to off board from the service.
3. [AWS IoT FleetWise Edge Agent Developer Guide](./docs/dev-guide/edge-agent-dev-guide.md) provides step-by-step instructions for building and running AWS IoT FleetWise Edge.
4. [AWS IoT FleetWise API Reference](https://docs.aws.amazon.com/iot-fleetwise/latest/APIReference/Welcome.html) describes all the API operations for FleetWise

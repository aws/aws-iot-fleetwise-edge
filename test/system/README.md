# System Tests

This folder contains test scripts for performing edge-to-cloud system tests. The test scripts are
written in Python and [pytest](https://docs.pytest.org).

Usually each test case does the following:

1. Create an AWS IoT Thing with new credentials
2. Start FWE
3. Send documents (for example, Decoder Manifest and Collection Scheme) to the IoT endpoint,
   mimicking what Cloud would do
4. Stimulate FWE by sending data on the buses that FWE is monitoring
5. Check that any data received from the IoT endpoint is as expected
6. Stop FWE and delete the IoT Thing

It is possible to execute the system tests in two different environments: Software-In-the-Loop (SIL)
and Hardware-In-the-Loop (HIL). The SIL tests require no extra hardware as everything is run inside
a simulated environment. The HIL tests must be run on a machine connected to CAN hardware interfaces
that are connected to the board under test.

The test scripts themselves are independent of the execution environment, i.e. the same test script
can run in either the SIL or HIL environment.

## Introduction to pytest

The tests use `pytest`, which is a test framework that moves away from the more conventional
frameworks. While xUnit-style frameworks like the ones found in many other languages and Python's
own built-in `unittest` module are based on inheriting from a base class, `pytest` promotes the
usage of `fixtures`, which are just functions marked with a decorator.

Fixtures are one of the most important concepts of `pytest`, and influences a lot how the framework
behaves. So it is important to have some understanding of pytest and its fixtures before working
with the system tests.

The following documentation is highly recommended:

- [Introduction to pytest](https://docs.pytest.org/en/latest/getting-started.html)
- [Introduction to fixtures](https://docs.pytest.org/en/latest/explanation/fixtures.html)
- [Detailed explanation on how to use fixtures](https://docs.pytest.org/en/latest/how-to/fixtures.html)

## Getting started on a development machine

This section describes how to get started on a development machine.

### Prerequisites for development machine

- Access to an AWS Account with administrator privileges.
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

### Compile your Edge Agent

1. Run the following _on the development machine_ to clone the latest FWE source code from GitHub.

   ```bash
   git clone https://github.com/aws/aws-iot-fleetwise-edge.git ~/aws-iot-fleetwise-edge \
   && cd ~/aws-iot-fleetwise-edge
   ```

1. Review, modify and supplement [the FWE source code](../../src/) to ensure it meets your use case
   and requirements.

1. Install the dependencies for FWE by running the commands below. Pass the required feature options
   when running the [install-deps-native.sh](../../tools/install-deps-native.sh) script

   ```bash
   sudo -H ./tools/system-test-setup.sh \
   && sudo -H ./tools/install-deps-native.sh
   ```

1. Compile FWE with respective feature required:

   ```bash
   ./tools/build-fwe-native.sh
   ```

1. Install python test requirements:

   ```bash
   cd ./test/system \
   && python3 -m pip install -r requirements.txt
   ```

### Running the tests

After installing the dependencies and compiling your Edge agent, run the tests as follows:

```bash
cd test/system
./testframework/gen.sh
mkdir -p run
TEST_FWE_BINARY=../../build/aws-iot-fleetwise-edge TEST_CREATE_NEW_AWS_THING=true \
PYTHONPATH=../../tools/cansim:../../tools/rossim:../../build:${PYTHONPATH} \
TMPDIR=run/ pytest test_heartbeat.py
```

You can then find the html report in `run/html_report/index.html` and other test artifacts like logs
and memcheck results in the `run` folder.

Depending on the tests you are running, additional environment variables might be required to be
passed in the `pytest` command above:

1. For the CAN to SOME/IP bridge tests, pass
   `TEST_CAN_TO_SOMEIP_BINARY=../../build/iotfleetwise/can-to-someip`.
1. For Greengrass tests, pass `TEST_GREENGRASS_JAR_PATH=/usr/local/greengrass/lib/Greengrass.jar`

### Logging

The tests use Python's built-in `logging` framework, which is automatically integrated with pytest
(see the [Tips](#tips) section below for some options).

By default we don't pass the log level to pytest. Instead we configure (with an autouse fixture in
[conftest.py](./conftest.py)) some default that is reasonable for most tests. This can then be
overridden with pytest's built-in options when needed (e.g. long running tests or when manually
running the tests).

### Temporary files generated by tests

The tests generate some files while running (e.g. FWE config, FWE logs, collection scheme, decoder
manifest). Those files are created using
[pytest's tmp_path_factory fixture](https://docs.pytest.org/en/latest/how-to/tmp_path.html) which by
default uses the system's temporary dir (i.e. `/tmp` on Linux). If you want to change the base temp
dir, set the `TMPDIR` env var, which changes
[Python's temp dir](https://docs.python.org/3/library/tempfile.html#tempfile.gettempdir) which is
then used by pytest.

If you need to investigate a test failure, you can navigate the temp dir to find the files generated
by a specific test. For example, for the latest run you can go to
`${TMPDIR}/pytest-of-root/pytest-current/<TEST_NAME>`.

### Running tests in parallel

The tests can be run in parallel with the
[pytest-xdist](https://pytest-xdist.readthedocs.io/en/latest/index.html) plugin (already installed).

To enable that you need to pass the `n<NUM_WORKERS>` command line arg.

The [pytest-random-order](https://github.com/pytest-dev/pytest-random-order) plugin can be used to
run the tests in random order. This is useful in revealing hidden dependencies between tests. If you
need to reproduce an exact test order, check the documentation.

**NOTE:** For all the tests to run in parallel in your machine, you need to create multiple `vcan`
interfaces (one per worker) and multiple network namespaces (see the
[setup-network-namespace.sh](./testframework/setup-network-namespace.sh) script).

For example:

```bash
NUM_WORKERS=18
./test/system/testframework/setup-network-namespace.sh --num-namespaces $NUM_WORKERS
cd test/system
mkdir run
./testframework/gen.sh
TEST_FWE_BINARY=../../build/aws-iot-fleetwise-edge TEST_CREATE_NEW_AWS_THING=true \
PYTHONPATH=../../tools/cansim:../../tools/rossim:../../build:${PYTHONPATH} \
TMPDIR=run/ pytest -n${NUM_WORKERS} --dist worksteal
```

### Tips

`pytest` offers a lot of built-in arguments to be passed to the command line. You can check the
following docs for an overview:

- [Basic patterns and examples](https://docs.pytest.org/en/latest/example/simple.html)
- [How to invoke pytest](https://docs.pytest.org/en/latest/how-to/usage.html)

But here is a quick reference for some commonly used arguments:

```bash
# Filter tests to be run by keyword expressions
pytest -k 'heartbeat and dtc'
# Run a specific test by its full name
pytest "test_checkins.py::TestCheckins::test_checkins[{'use_faketime': False}]"
# Just list available tests without running them
pytest --collect-only
# Run tests in parallel
pytest -n4
# Output logs, stdout and stderr while the tests run (-o overrides options from pytest.ini).
# See https://docs.pytest.org/en/latest/how-to/logging.html and https://docs.pytest.org/en/latest/how-to/capture-stdout-stderr.html
pytest -o "log_cli=true" --capture=tee-sys
# Change level of logs captured by pytest (changing to DEBUG will show messages from botocore and socketcan in the report, but it can be very verbose)
pytest --log-level=DEBUG
# Disable a particular logger
pytest --log-level=DEBUG --log-disable=can.interfaces.socketcan.socketcan.tx
```

### Suppress the memcheck errors

If you need to suppress the false positive memcheck errors, put them in the `valgrind.supp` file.
You could add this option `--gen-suppressions=all --log-file=generate-suppression-file.log` to the
valgrind command. After running the system test, in the output file:
`generate-suppression-file.log`, it contains the items you want to suppress. Then copy them into our
system test `valgrind.supp`

## SIL Environment

In the SIL environment, the test script executes the native (x86) version of FWE and configured with
one or more virtual CAN buses. The messages sent on the network interfaces are controlled by the
test script, allowing data collection to be triggered according to the test scenario. The collected
data is sent by FWE to the IoT endpoint and then received by the test script which is also
subscribed to the endpoint.

```
----------------------------------------------------------------   ----------------
| Test Runner                                                  |   | AWS IoT Core |
|                                           vcan0-N  *******-->+-->+---------     |
| **canigen**--------------------------------------->**FWE**   |   |        |     |
|      ^                                             *******<--+<--+------  |     |
|      |                                                       |   |     |  |     |
|      |       -------------------------------------------------   |     |  |     |
|      |       |                                                   |     |  |     |
| ** Test **   |                                                   |     |  |     |
| **Script**   |                                                   |     |  |     |
|   ^    |     |  Decoder Manifest & Data Collection Schemes       |     |  |     |
|   |    ----->+-------------------------------------------------->+------  |     |
|   |          |  Collected Data                                   |        |     |
|   -----------+<--------------------------------------------------+<--------     |
|              |                                                   |              |
----------------                                                   ----------------
```

## HIL Environment

In the HIL environment, the FWE target binary, the credentials and the configuration are firstly
transferred via scp to the target board. The test script executes FWE on a target board via SSH
configured with one or more hardware CAN buses. The machine executing the test script is connected
to one or more hardware CAN interfaces that connect to the matching hardware interface of the target
board. The messages sent on the network interfaces are controlled by the test script, allowing data
collection to be triggered according to the test scenario. The collected data is sent by FWE to the
IoT endpoint and then received by the test script which is also subscribed to the endpoint.

```
----------------                                                   ----------------
| Test Runner  |  USB/      ----------------         -----------   | AWS IoT Core |
|              |  Ethernet  |   Intrepid   | can0-N  | Target  |-->+---------     |
| **canigen**->+----------->| CAN/Ethernet |-------->| Board   |   |        |     |
|      ^       |            |     Box      |         | **FWE** |<--+<-----  |     |
|      |       |            ----------------         -----------   |     |  |     |
|      |       |                                          ^        |     |  |     |
|      |       |  SSH                                     |        |     |  |     |
| ** Test **-->+-------------------------------------------        |     |  |     |
| **Script**   |                                                   |     |  |     |
|   ^    |     |  Decoder Manifest & Data Collection Schemes       |     |  |     |
|   |    ----->+-------------------------------------------------->+------  |     |
|   |          |  Collected Data                                   |        |     |
|   -----------+<--------------------------------------------------+<--------     |
|              |                                                   |              |
----------------                                                   ----------------
```

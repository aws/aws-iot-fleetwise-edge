# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

FROM public.ecr.aws/ubuntu/ubuntu:20.04

# Provided by buildx, e.g.: 'linux/amd64', 'linux/arm64', etc.:
ARG TARGETPLATFORM

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        ca-certificates \
        iproute2 \
        jq \
    && rm -rf /var/lib/apt/lists/*

COPY ${TARGETPLATFORM}/src/executionmanagement/aws-iot-fleetwise-edge /usr/bin/
COPY tools/container/start-fwe.sh                                     /usr/bin
COPY tools/configure-fwe.sh                                           /usr/bin/
COPY LICENSE                                                          /usr/share/doc/aws-iot-fleetwise-edge/
COPY THIRD-PARTY-LICENSES                                             /usr/share/doc/aws-iot-fleetwise-edge/
COPY configuration/static-config.json                                 /usr/share/aws-iot-fleetwise/

ENTRYPOINT ["/usr/bin/start-fwe.sh"]

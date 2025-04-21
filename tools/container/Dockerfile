# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

FROM public.ecr.aws/ubuntu/ubuntu:22.04 AS temp

# Provided by buildx, e.g.: 'linux/amd64', 'linux/arm64', etc.:
ARG TARGETPLATFORM

COPY ${TARGETPLATFORM}/aws-iot-fleetwise-edge.tar.gz  /tmp/
COPY ${TARGETPLATFORM}/opt.tar.gz                     /tmp/
RUN tar -zxf /tmp/aws-iot-fleetwise-edge.tar.gz -C /usr/bin \
    && tar -zxf /tmp/opt.tar.gz -C /

FROM public.ecr.aws/ubuntu/ubuntu:22.04
ENV DEBIAN_FRONTEND="noninteractive"
SHELL ["/bin/bash", "-c"]

ARG DIST_NAME

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        ca-certificates \
        iproute2 \
        jq \
    && if [ "${DIST_NAME}" == "aws-iot-fleetwise-edge-ros2" ]; then \
        apt-get install --no-install-recommends -y python3 libspdlog1; \
    fi \
    && rm -rf /var/lib/apt/lists/*

COPY --from=temp /opt/                           /opt/
COPY --from=temp /usr/bin/aws-iot-fleetwise-edge /usr/bin/
COPY tools/container/start-fwe.sh                /usr/bin/
COPY tools/configure-fwe.sh                      /usr/bin/
COPY LICENSE                                     /usr/share/doc/aws-iot-fleetwise-edge/
COPY THIRD-PARTY-LICENSES                        /usr/share/doc/aws-iot-fleetwise-edge/
COPY configuration/static-config.json            /usr/share/aws-iot-fleetwise/

ENTRYPOINT ["/usr/bin/start-fwe.sh"]

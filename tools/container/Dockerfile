FROM ubuntu:18.04 as build-step

ARG version=v1.0.0
LABEL version=${version}

RUN apt-get update && \
    apt-get install --no-install-recommends -y \
    git ca-certificates iproute2 can-utils jq sed && \
    apt-get clean
WORKDIR /root
RUN git clone --depth 1 --branch ${version} https://github.com/aws/aws-iot-fleetwise-edge.git && \
    cd /root/aws-iot-fleetwise-edge/ && \
    sed -i 's/sudo//g' ./tools/install-deps-native.sh && \
    ./tools/install-deps-native.sh && \
    ./tools/build-fwe-native.sh 

FROM ubuntu:18.04

LABEL version=${version}

RUN apt-get update && \
    apt-get install --no-install-recommends -y \
    ca-certificates iproute2 can-utils jq sed \
    curl zlib1g-dev libcurl4-openssl-dev && \
    apt-get clean
WORKDIR /root

COPY --from=build-step /root/aws-iot-fleetwise-edge/build/src/executionmanagement/aws-iot-fleetwise-edge /usr/bin/
COPY --from=build-step /root/aws-iot-fleetwise-edge/tools/configure-fwe.sh /usr/bin/
COPY --from=build-step /root/aws-iot-fleetwise-edge/configuration/static-config.json /etc/
COPY LICENCE /usr/share/doc/aws-iot-fleetwise-edge/
COPY THIRD-PARTY-LICENSES /usr/share/doc/aws-iot-fleetwise-edge/

ADD tools/container/start-fwe.sh /usr/bin
RUN chmod +x /usr/bin/start-fwe.sh

ENTRYPOINT /usr/bin/start-fwe.sh
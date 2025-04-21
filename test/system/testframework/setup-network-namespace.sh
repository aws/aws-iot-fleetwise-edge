#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set -euo pipefail

IFACE=$(ip route ls | grep default | sed 's/.*dev //' | awk '{print $1}' | head -n1)
ONLY_CLEAN_UP=false
# By default, 1 for each vcan that we create in EC2 hosts (vcan0 to vcan17)
NUM_NAMESPACES=18

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --only-clean-up)
            ONLY_CLEAN_UP=true
            ;;
        --num-namespaces)
            NUM_NAMESPACES=$2
            shift
            ;;
        esac
        shift
    done
}

parse_args "$@"

iptables -P FORWARD DROP
iptables -F FORWARD
iptables -t nat -F

for i in $(seq 0 $((NUM_NAMESPACES-1))); do
    NS=ns$i
    VPEER=vpeer$i
    VXCANPEER=vxcanpeer$i
    VXCAN=vxcan$i
    NETWORK=10.200.$((i+1)).0/24
    VETH_ADDR=10.200.$((i+1)).1
    VPEER_ADDR=10.200.$((i+1)).2
    VETH=veth$i
    CANIN=vcan$i

    echo "using $IFACE interface for internet connection into namespace $NS"
    echo "using $CANIN interface for can traffic into namespace $NS"

    VPEER_UDP_RULE="INPUT -p udp -s ${VPEER_ADDR} -j ACCEPT"

    cleanup() {
        ip netns del ${NS} &>/dev/null || true
        ip link delete ${VPEER} &>/dev/null || true
        ip link delete ${VXCANPEER} &>/dev/null || true
        ip link delete ${VXCAN} &>/dev/null || true
        ip link delete ${VETH} &>/dev/null || true
        while ip route del ${NETWORK} 2>/dev/null; do :; done

        iptables -D ${VPEER_UDP_RULE} 2>/dev/null || true
    }

    cleanup

    if [ "${ONLY_CLEAN_UP}" == false ]; then
        ip netns add ${NS}
        ip link add ${VETH} type veth peer name ${VPEER}
        ip link set ${VPEER} netns ${NS}

        ip link add ${VXCAN} type vxcan peer name ${VXCANPEER}
        ip link set ${VXCANPEER} netns ${NS}
        ip link set ${VXCAN} up
        ip netns exec ${NS} ip link set ${VXCANPEER} up

        ip addr add ${VETH_ADDR}/24 dev ${VETH}
        ip link set ${VETH} up
        ip netns exec ${NS} ip addr add ${VPEER_ADDR}/24 dev ${VPEER}
        ip netns exec ${NS} ip link set ${VPEER} up
        ip netns exec ${NS} ip link set lo up
        ip netns exec ${NS} ip route add default via ${VETH_ADDR}
        echo 1 > /proc/sys/net/ipv4/ip_forward

        # This is necessary to allow the FWE process running inside the network namespace to communicate
        # with the ROS2 node (e.g. rosigen) running outside.
        iptables -A ${VPEER_UDP_RULE}

        # iptables -t nat -A POSTROUTING  -j LOG --log-prefix "++++ nat, postrouting ++++"
        iptables -t nat -A POSTROUTING -s ${VPEER_ADDR}/24 -o ${IFACE} -j MASQUERADE
        # iptables -A FORWARD -j LOG --log-prefix "++++ forward ++++"
        iptables -A FORWARD -i ${IFACE} -o ${VETH} -j ACCEPT
        iptables -A FORWARD -o ${IFACE} -i ${VETH} -j ACCEPT

        # If this ping fails, the network namespace is not properly configured.
        # You can try the following to troubleshoot:
        # 1. Run the script again passing --only-clean-up both inside the docker image and on the host.
        #    Depending on the docker image, the command `iptables` inside the image may use legacy
        #    tables which in the host would be modified by running iptables-legacy command instead.
        #    That is why running the clean-up on both may be helpful.
        # 2. Then run the setup script again. If your host is different than the docker image, run first
        #    on the host, and then on the docker image. This is due to the possible difference between
        #    the iptables versions mentioned above. If the version of iptables inside the docker
        #    image is old, it will add the rules only to the legacy tables, but the host might
        #    contain other rules that drop the packets before reaching the legacy tables.
        #
        # If that still doesn't work, the following might at least help you pinpoint where packets are
        # being dropped or routed to the wrong interface:
        #    1. Run `sudo watch -n1 'iptables-save -c'` on a separate terminal either on the docker
        #       image or host (in this case you might need to run iptables-legacy-save instead).
        #    2. In another terminal keep a ping command running: `sudo ip netns exec ns<NUMBER> ping amazon.com`
        #       Note that you should run the ping on the environment where you are trying to use the network
        #       namespace.
        #    3. Observe the output of the `watch` command. When everything is fine you should see all
        #       three rules (1x postrouting and 2x forward) with their counters being incremented on every ping
        #       attempt. If some counters are not incrementing you now know where this setup starts to break.
        #    4. Additionally you can uncomment the `LOG` commands in this script and then check the logs
        #       on the host with `sudo journalctl -f | grep ++++`. This may help you check whether packets
        #       are being correctly routed. For example, if the forwarded traffic is being routed to the
        #       wrong interface, you will be able to see in the logs.
        #
        ip netns exec ${NS} ping -c 1 amazon.com
    fi
done

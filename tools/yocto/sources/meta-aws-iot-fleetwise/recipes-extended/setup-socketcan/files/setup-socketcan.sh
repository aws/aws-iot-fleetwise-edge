#!/bin/sh
ip link add dev vcan0  type vcan; ip link set up vcan0
ip link add dev vcan1  type vcan; ip link set up vcan1
ip link set up can0 txqueuelen 1000 type can bitrate 500000 dbitrate 5000000 fd on restart-ms 100
ip link set up can1 txqueuelen 1000 type can bitrate 500000 dbitrate 5000000 fd on restart-ms 100

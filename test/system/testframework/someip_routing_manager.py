# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

if __name__ == "__main__":
    import signal
    import sys
    import time

    from someipigen import SignalManager

    signal.signal(signal.SIGTERM, signal.getsignal(signal.SIGINT))

    sys.stdout.write("Starting SignalManager\n")
    sys.stdout.flush()
    someip_routing_manager = SignalManager()
    someip_routing_manager.start("local", "RoutingManager", "someipigen")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        sys.stdout.write("Stopping SignalManager\n")
        sys.stdout.flush()
        someip_routing_manager.stop()

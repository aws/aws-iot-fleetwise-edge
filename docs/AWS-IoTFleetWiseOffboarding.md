## Offboarding and Data Deletion

To delete all of your "free text data" from an ECU running the Reference Implementation for AWS IoT
FleetWise ("FWE"), please run the following commands:

1. Stop FWE

   ```bash
   sudo systemctl stop fwe@0
   ```

2. Disable FWE

   ```bash
   sudo systemctl disable fwe@0
   ```

3. Delete the persistent data files for FWE

   ```bash
   sudo rm -f /var/aws-iot-fleetwise/*
   ```

4. Delete the configuration files for FWE

   ```bash
   sudo rm -f /etc/aws-iot-fleetwise/*
   ```

// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

package com.aws.iotfleetwise;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.Intent;
import android.os.Bundle;

import android.os.ParcelUuid;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;

import java.util.Set;

public class BluetoothActivity
        extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_bluetooth_device_list);
        setResult(Activity.RESULT_CANCELED);
        ArrayAdapter<String> deviceListAdapter = new ArrayAdapter<>(this, R.layout.bluetooth_device);
        ListView deviceListView = findViewById(R.id.device_list);
        deviceListView.setAdapter(deviceListAdapter);
        try {
            BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
            if (adapter == null || !adapter.isEnabled()) {
                deviceListAdapter.add("Bluetooth disabled");
                return;
            }
            Set<BluetoothDevice> devices = adapter.getBondedDevices();
            for (BluetoothDevice device : devices) {
                ParcelUuid[] uuids = device.getUuids();
                for (ParcelUuid uuid : uuids) {
                    if (uuid.getUuid().equals(Elm327.SERIAL_PORT_UUID)) {
                        deviceListAdapter.add(device.getName() + "\t" + device.getAddress());
                    }
                }
            }
            if (deviceListAdapter.getCount() == 0) {
                deviceListAdapter.add("No supported devices");
                return;
            }
        }
        catch (SecurityException e) {
            deviceListAdapter.add("Bluetooth access denied");
            return;
        }
        deviceListView.setOnItemClickListener((parent, view, position, id) -> {
            Intent intent = new Intent();
            intent.putExtra("bluetooth_device", ((TextView)view).getText().toString());
            setResult(Activity.RESULT_OK, intent);
            finish();
        });
    }
}

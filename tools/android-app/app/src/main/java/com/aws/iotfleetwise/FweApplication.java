// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

package com.aws.iotfleetwise;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.util.Log;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

public class FweApplication
        extends Application
        implements
        SharedPreferences.OnSharedPreferenceChangeListener,
        LocationListener {
    private static final String LOCATION_PROVIDER = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            ? LocationManager.FUSED_PROVIDER
            : LocationManager.GPS_PROVIDER;

    private SharedPreferences mPrefs = null;
    private Elm327 mElm327 = null;
    private LocationManager mLocationManager = null;
    private Location mLastLocation = null;
    private List<Integer> mSupportedPids = null;
    private final Object mSupportedPidsLock = new Object();

    @Override
    public void onLocationChanged(Location loc) {
        Log.i("FweApplication", "Location change: Lat: " + loc.getLatitude() + " Long: " + loc.getLongitude());
    }

    @Override
    public void onStatusChanged(String s, int i, Bundle bundle) {
        Log.i("FweApplication", "Location status changed");
    }

    @Override
    public void onProviderEnabled(String s) {
        Log.i("FweApplication", "Location provider enabled: " + s);
    }

    @Override
    public void onProviderDisabled(String s) {
        Log.i("FweApplication", "Location provider disabled: " + s);
    }

    @Override
    public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key) {
        if (!mPrefs.getString("vehicle_name", "").equals("")
                && !mPrefs.getString("mqtt_endpoint_url", "").equals("")
                && !mPrefs.getString("mqtt_certificate", "").equals("")
                && !mPrefs.getString("mqtt_private_key", "").equals("")
                && !mFweThread.isAlive()) {
            mFweThread.start();
        }
        mDataAcquisitionThread.interrupt();
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mPrefs = PreferenceManager.getDefaultSharedPreferences(this);
        mPrefs.registerOnSharedPreferenceChangeListener(this);
        onSharedPreferenceChanged(null, null);
        mElm327 = new Elm327();
        mDataAcquisitionThread.start();
    }

    void requestLocationUpdates() {
        if (mLocationManager == null) {
            Log.i("FweApplication", "Requesting location access");
            mLocationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
            try {
                mLocationManager.requestLocationUpdates(LOCATION_PROVIDER, 5000, 10, this);
            } catch (SecurityException e) {
                Log.e("FweApplication", "Location access denied");
                mLocationManager = null;
            }
        }
    }

    void serviceLocation() {
        if (mLocationManager == null) {
            return;
        }
        try {
            mLastLocation = mLocationManager.getLastKnownLocation(LOCATION_PROVIDER);
            if (mLastLocation == null) {
                Log.d("FweApplication", "Location unknown");
                return;
            }
            Fwe.setLocation(mLastLocation.getLatitude(), mLastLocation.getLongitude());
        }
        catch (SecurityException e) {
            Log.d("FweApplication", "Location access denied");
        }
    }

    private String getLocationSummary()
    {
        if (mLastLocation == null) {
            return "UNKNOWN";
        }
        return String.format(Locale.getDefault(), "%f, %f", mLastLocation.getLatitude(), mLastLocation.getLongitude());
    }

    private int getUpdateTime()
    {
        int updateTime = R.string.default_update_time;
        try {
            updateTime = Integer.parseInt(mPrefs.getString("update_time", String.valueOf(R.string.default_update_time)));
        }
        catch (Exception ignored) {
        }
        if (updateTime == 0) {
            updateTime = R.string.default_update_time;
        }
        updateTime *= 1000;
        return updateTime;
    }

    private static int chrToNibble(char chr)
    {
        int res;
        if (chr >= '0' && chr <= '9') {
            res = chr - '0';
        }
        else if (chr >= 'A' && chr <= 'F') {
            res = 10 + chr - 'A';
        }
        else {
            res = -1; // Invalid hex char
        }
        return res;
    }

    private static int[] convertResponse(String response)
    {
        List<Integer> responseList = new ArrayList<>();
        for (int i = 0; (i + 1) < response.length(); i+=2)
        {
            int highNibble = chrToNibble(response.charAt(i));
            int lowNibble = chrToNibble(response.charAt(i+1));
            if (highNibble < 0 || lowNibble < 0)
            {
                return null;
            }
            responseList.add((highNibble << 4) + lowNibble);
            // Skip over spaces:
            if ((i + 2) < response.length() && response.charAt(i+2) == ' ') {
                i++;
            }
        }
        // Convert list to array:
        return responseList.stream().mapToInt(Integer::intValue).toArray();
    }

    Thread mDataAcquisitionThread = new Thread(() -> {
        while (true) {
            Log.i("FweApplication", "Starting data acquisition");
            String bluetoothDevice = mPrefs.getString("bluetooth_device", "");

            serviceOBD(bluetoothDevice);
            serviceLocation();

            // Wait for update time:
            try {
                Thread.sleep(getUpdateTime());
            } catch (InterruptedException e) {
                // Carry on
            }
        }
    });

    private void serviceOBD(String bluetoothDevice)
    {
        mElm327.connect(bluetoothDevice);
        if (!checkVehicleConnected()) {
            return;
        }
        int[] pidsToRequest = Arrays.stream(Fwe.getObdPidsToRequest()).sorted().toArray();
        if (pidsToRequest.length == 0) {
            return;
        }
        List<Integer> supportedPids = new ArrayList<>();
        for (int pid : pidsToRequest) {
            if ((mSupportedPids != null) && !mSupportedPids.contains(pid)) {
                continue;
            }
            Log.i("FweApplication", String.format("Requesting PID: 0x%02X", pid));
            String request = String.format("01 %02X", pid);
            String responseString = mElm327.sendObdRequest(request);
            int[] responseBytes = convertResponse(responseString);
            if ((responseBytes == null) || (responseBytes.length == 0)) {
                Log.e("FweApplication", String.format("No response for PID: 0x%02X", pid));
                // If vehicle is disconnected:
                if (mSupportedPids != null) {
                    synchronized (mSupportedPidsLock) {
                        mSupportedPids = null;
                    }
                    return;
                }
            }
            else {
                supportedPids.add(pid);
                Fwe.setObdPidResponse(pid, responseBytes);
            }
        }
        if ((mSupportedPids == null) && (supportedPids.size() > 0)) {
            StringBuilder sb = new StringBuilder();
            for (int b : supportedPids) {
                sb.append(String.format("%02X ", b));
            }
            Log.i("FweApplication", "Supported PIDs: " + sb.toString());
            synchronized (mSupportedPidsLock) {
                mSupportedPids = supportedPids;
            }
        }
    }

    private boolean checkVehicleConnected()
    {
        if (mSupportedPids != null) {
            return true;
        }
        Log.i("FweApplication", "Checking if vehicle connected...");
        String response = mElm327.sendObdRequest(Elm327.CMD_OBD_SUPPORTED_PIDS_0);
        int[] responseBytes = convertResponse(response);
        boolean result = (responseBytes != null) && (responseBytes.length > 0);
        Log.i("FweApplication", "Vehicle is " + (result ? "CONNECTED" : "DISCONNECTED"));
        return result;
    }

    Thread mFweThread = new Thread(() -> {
        Log.i("FweApplication", "Starting FWE");
        String vehicleName = mPrefs.getString("vehicle_name", "");
        String endpointUrl = mPrefs.getString("mqtt_endpoint_url", "");
        String certificate = mPrefs.getString("mqtt_certificate", "");
        String privateKey = mPrefs.getString("mqtt_private_key", "");
        String mqttTopicPrefix = mPrefs.getString("mqtt_topic_prefix", "");
        int res = Fwe.run(
                getAssets(),
                vehicleName,
                endpointUrl,
                certificate,
                privateKey,
                mqttTopicPrefix);
        if (res != 0)
        {
            Log.e("FweApplication", String.format("FWE exited with code %d", res));
        }
        else {
            Log.i("FweApplication", "FWE finished");
        }
    });

    public String getStatusSummary()
    {
        String supportedPids;
        synchronized (mSupportedPidsLock) {
            if (mSupportedPids == null) {
                supportedPids = "VEHICLE DISCONNECTED";
            }
            else if (mSupportedPids.size() == 0) {
                supportedPids = "NONE";
            }
            else {
                StringBuilder sb = new StringBuilder();
                for (int pid : mSupportedPids) {
                    sb.append(String.format("%02X ", pid));
                }
                supportedPids = sb.toString();
            }
        }
        return "Bluetooth: " + mElm327.getStatus() + "\n\n"
                + "Supported OBD PIDs: " + supportedPids + "\n\n"
                + "Location: " + getLocationSummary() + "\n\n"
                + Fwe.getStatusSummary();
    }
}

// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

package com.aws.iotfleetwise;

import android.annotation.SuppressLint;
import android.app.Application;
import android.car.Car;
import android.car.VehiclePropertyIds;
import android.car.hardware.CarPropertyConfig;
import android.car.hardware.CarPropertyValue;
import android.car.hardware.property.CarPropertyManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Build;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.util.Log;

import java.lang.reflect.Array;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

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
    private final Object mSupportedSignalsLock = new Object();
    private CarPropertyManager mCarPropertyManager = null;
    private boolean mReadVehicleProperties = false;
    private List<String> mSupportedVehicleProperties = null;

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
        // Try creating the CarPropertyManager to detect whether we are running on Android Automotive
        try {
            mCarPropertyManager = (CarPropertyManager)Car.createCar(this).getCarManager(Car.PROPERTY_SERVICE);
        }
        catch (NoClassDefFoundError ignored) {
            // Not Android Automotive, fall back to ELM327 mode
            mElm327 = new Elm327();
        }
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
        int[] arr = new int[responseList.size()];
        for (int i = 0; i < responseList.size(); i++) {
            arr[i] = responseList.get(i);
        }
        return arr;
    }

    Thread mDataAcquisitionThread = new Thread(() -> {
        while (true) {
            Log.i("FweApplication", "Starting data acquisition");

            if (isCar()) {
                serviceCarProperties();
            }
            else {
                String bluetoothDevice = mPrefs.getString("bluetooth_device", "");
                serviceOBD(bluetoothDevice);
            }
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
        int[] pidsToRequest = Fwe.getObdPidsToRequest();
        if (pidsToRequest.length == 0) {
            return;
        }
        Arrays.sort(pidsToRequest);
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
                    synchronized (mSupportedSignalsLock) {
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
            synchronized (mSupportedSignalsLock) {
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

    private int[] getVehiclePropertyIds(int[][] vehiclePropertyInfo)
    {
        Set<Integer> propIds = new LinkedHashSet<>();
        for (int[] info : vehiclePropertyInfo)
        {
            propIds.add(info[0]);
        }
        int[] arr = new int[propIds.size()];
        int i = 0;
        for (Integer id : propIds)
        {
            arr[i++] = id;
        }
        return arr;
    }

    private int getVehiclePropertySignalId(int[][] vehiclePropertyInfo, int propId, int areaIndex, int resultIndex)
    {
        for (int[] info : vehiclePropertyInfo)
        {
            if ((propId == info[0]) && (areaIndex == info[1]) && (resultIndex == info[2]))
            {
                return info[3];
            }
        }
        return -1;
    }

    @SuppressLint("DefaultLocale")
    private void serviceCarProperties()
    {
        List<String> supportedProps = new ArrayList<>();
        int[][] propInfo = Fwe.getVehiclePropertyInfo();
        int[] propIds = getVehiclePropertyIds(propInfo);
        for (int propId : propIds) {
            String propName = VehiclePropertyIds.toString(propId);
            CarPropertyConfig config = mCarPropertyManager.getCarPropertyConfig(propId);
            if (config == null) {
                Log.d("serviceCarProperties", "Property unavailable: "+propName);
                continue;
            }
            int[] areaIds = config.getAreaIds();
            Class<?> clazz = config.getPropertyType();
            for (int areaIndex = 0; areaIndex < areaIds.length; areaIndex++) {
                int signalId = getVehiclePropertySignalId(propInfo, propId, areaIndex, 0);
                if (signalId < 0) {
                    Log.d("serviceCarProperties", String.format("More area IDs (%d) than expected (%d) for %s", areaIds.length, areaIndex + 1, propName));
                    break;
                }
                CarPropertyValue propVal;
                try {
                    propVal = mCarPropertyManager.getProperty(clazz, propId, areaIds[areaIndex]);
                } catch (IllegalArgumentException ignored) {
                    Log.w("serviceCarProperties", String.format("Could not get %s 0x%X", propName, areaIds[areaIndex]));
                    continue;
                } catch (SecurityException e) {
                    Log.w("serviceCarProperties", String.format("Access denied for %s 0x%X", propName, areaIds[areaIndex]));
                    continue;
                }
                if (areaIndex == 0) {
                    supportedProps.add(propName);
                }
                StringBuilder sb = new StringBuilder();
                sb.append(String.format("%s 0x%X: ", propName, areaIds[areaIndex]));
                if (clazz.equals(Boolean.class)) {
                    double val = (boolean) propVal.getValue() ? 1.0 : 0.0;
                    sb.append(val);
                    Fwe.setVehicleProperty(signalId, val);
                } else if (clazz.equals(Integer.class) || clazz.equals(Float.class)) {
                    double val = ((Number)propVal.getValue()).doubleValue();
                    sb.append(val);
                    Fwe.setVehicleProperty(signalId, val);
                } else if (clazz.equals(Integer[].class) || clazz.equals(Long[].class)) {
                    sb.append("[");
                    for (int resultIndex = 0; resultIndex < Array.getLength(propVal.getValue()); resultIndex++) {
                        if (resultIndex > 0) {
                            signalId = getVehiclePropertySignalId(propInfo, propId, areaIndex, resultIndex);
                            if (signalId < 0) {
                                Log.d("serviceCarProperties", String.format("More results (%d) than expected (%d) for %s 0x%X", Array.getLength(propVal.getValue()), resultIndex + 1, propName, areaIds[areaIndex]));
                                break;
                            }
                        }
                        double val = ((Number)Array.get(propVal.getValue(), resultIndex)).doubleValue();
                        if (resultIndex > 0) {
                            sb.append(", ");
                        }
                        sb.append(val);
                        Fwe.setVehicleProperty(signalId, val);
                    }
                    sb.append("]");
                } else {
                    Log.w("serviceCarProperties", "Unsupported type " + clazz.toString() + " for " + propName);
                    continue;
                }
                Log.i("serviceCarProperties", sb.toString());
            }
        }
        if ((mSupportedVehicleProperties == null) && (supportedProps.size() > 0)) {
            Collections.sort(supportedProps);
            synchronized (mSupportedSignalsLock) {
                mSupportedVehicleProperties = supportedProps;
            }
        }
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
        StringBuilder sb = new StringBuilder();
        synchronized (mSupportedSignalsLock) {
            if (isCar()) {
                if (mSupportedVehicleProperties != null) {
                    if (mSupportedVehicleProperties.size() == 0) {
                        sb.append("NONE");
                    } else {
                        sb.append("Supported vehicle properties: ")
                                .append(String.join(", ", mSupportedVehicleProperties));
                    }
                }
            }
            else {
                sb.append("Bluetooth: ")
                        .append(mElm327.getStatus())
                        .append("\n\n")
                        .append("Supported OBD PIDs: ");
                if (mSupportedPids == null) {
                    sb.append("VEHICLE DISCONNECTED");
                } else if (mSupportedPids.size() == 0) {
                    sb.append("NONE");
                } else {
                    for (int pid : mSupportedPids) {
                        sb.append(String.format("%02X ", pid));
                    }
                }
            }
        }
        sb.append("\n\n")
                .append("Location: ")
                .append(getLocationSummary())
                .append("\n\n")
                .append(Fwe.getStatusSummary());
        return sb.toString();
    }

    public boolean isCar() {
        return mCarPropertyManager != null;
    }
}

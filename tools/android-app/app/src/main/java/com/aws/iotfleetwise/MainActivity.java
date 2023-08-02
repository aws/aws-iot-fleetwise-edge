// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

package com.aws.iotfleetwise;

import android.car.Car;
import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.util.JsonReader;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.TextView;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

import pub.devrel.easypermissions.AfterPermissionGranted;
import pub.devrel.easypermissions.EasyPermissions;

public class MainActivity extends PreferenceActivity
    implements SharedPreferences.OnSharedPreferenceChangeListener
{
    private static final int REQUEST_PERMISSIONS = 100;
    private static final int REQUEST_BLUETOOTH = 200;
    private static final int REQUEST_CONFIGURE_VEHICLE = 300;
    private static final int REQUEST_ABOUT = 400;
    SharedPreferences mPrefs = null;

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this);
    }

    @AfterPermissionGranted(REQUEST_PERMISSIONS) // This causes this function to be called again after the user has approved access
    private void requestPermissions()
    {
        List<String> perms = new ArrayList<>();
        perms.add(Manifest.permission.ACCESS_FINE_LOCATION);
        String rationale = "Location";
        if (((FweApplication)getApplication()).isCar()) {
            perms.add(Car.PERMISSION_ENERGY);
            perms.add(Car.PERMISSION_SPEED);
            rationale += " and car information";
        }
        else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            perms.add(Manifest.permission.BLUETOOTH_CONNECT);
            rationale += " and Bluetooth";
        }
        String[] permsArray = perms.toArray(new String[0]);
        if (!EasyPermissions.hasPermissions(this, permsArray)) {
            Log.i("requestPermissions", "Requesting permissions");
            EasyPermissions.requestPermissions(this, rationale+" access required", REQUEST_PERMISSIONS, permsArray);
        }
        else {
            Log.i("requestPermissions", "Permissions granted, starting data acquisition");
            ((FweApplication)getApplication()).requestLocationUpdates();
        }
    }

    @Override
    public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key) {
        if (!((FweApplication)getApplication()).isCar()) {
            findPreference("bluetooth_device").setSummary(mPrefs.getString("bluetooth_device", "No device selected"));
        }
        findPreference("vehicle_name").setSummary(mPrefs.getString("vehicle_name", "Not yet configured"));
        findPreference("update_time").setSummary(mPrefs.getString("update_time", String.valueOf(R.string.default_update_time)));
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        switch (requestCode) {
            case REQUEST_BLUETOOTH:
                if (resultCode == Activity.RESULT_OK) {
                    String deviceParam = data.getExtras().getString("bluetooth_device");
                    SharedPreferences.Editor edit = mPrefs.edit();
                    edit.putString("bluetooth_device", deviceParam);
                    edit.apply();
                }
                break;
            case REQUEST_CONFIGURE_VEHICLE:
                if (resultCode == Activity.RESULT_OK) {
                    String link = data.getExtras().getString("provisioning_link");
                    downloadCredentials(link);
                }
                break;
        }
    }

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.xml.preferences);
        setContentView(R.layout.activity_main);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        TextView versionTextView = (TextView)findViewById(R.id.version);
        versionTextView.setText(Fwe.getVersion());
        mPrefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        mPrefs.registerOnSharedPreferenceChangeListener(this);
        requestPermissions();
        onSharedPreferenceChanged(null, null);
        Preference bluetoothDevicePreference = (Preference)findPreference("bluetooth_device");
        if (((FweApplication)getApplication()).isCar()) {
            PreferenceScreen preferenceScreen = getPreferenceScreen();
            preferenceScreen.removePreference(bluetoothDevicePreference);
        }
        else {
            bluetoothDevicePreference.setOnPreferenceClickListener(preference -> {
                startActivityForResult(new Intent(MainActivity.this, BluetoothActivity.class), REQUEST_BLUETOOTH);
                return false;
            });
            Preference vehicleNamePreference = (Preference)findPreference("vehicle_name");
            vehicleNamePreference.setOnPreferenceClickListener(preference -> {
                startActivityForResult(new Intent(MainActivity.this, ConfigureVehicleActivity.class), REQUEST_CONFIGURE_VEHICLE);
                return false;
            });
        }
        mStatusUpdateThread.start();

        // Handle deep link:
        Uri appLinkData = getIntent().getData();
        if (appLinkData != null) {
            downloadCredentials(appLinkData.toString());
        }
        // Handle ADB provided credentials:
        String credentials = getIntent().getStringExtra("credentials");
        if (credentials != null) {
            InputStream inputStream = new ByteArrayInputStream(credentials.getBytes(StandardCharsets.UTF_8));
            try {
                configureCredentials(inputStream);
            } catch (IOException ignored) {
                runOnUiThread(() -> {
                    AlertDialog alertDialog = new AlertDialog.Builder(MainActivity.this).create();
                    alertDialog.setTitle("Error");
                    alertDialog.setMessage("Invalid credentials");
                    alertDialog.setButton(AlertDialog.BUTTON_NEUTRAL, "OK", (dialog, which) -> dialog.dismiss());
                    alertDialog.show();
                });
            }
        }
    }

    @Override
    public void onDestroy() {
        if (mStatusUpdateThread != null) {
            mStatusUpdateThread.interrupt();
        }
        if (mPrefs != null) {
            mPrefs.unregisterOnSharedPreferenceChangeListener(this);
        }
        super.onDestroy();
    }

    public void onAboutClick(View v) {
        startActivityForResult(new Intent(MainActivity.this, AboutActivity.class), REQUEST_ABOUT);
    }

    private void downloadCredentials(String provisioningLink)
    {
        Thread t = new Thread(() -> {
            Log.i("DownloadCredentials", "Provisioning link: " + provisioningLink);
            final String urlParam = "url=";

            // First try getting the S3 link normally from the provisioning link:
            Uri uri = Uri.parse(provisioningLink);
            String fragment = uri.getFragment();
            if (fragment != null) {
                int urlStart = fragment.indexOf(urlParam);
                if (urlStart >= 0) {
                    String s3Link = fragment.substring(urlStart + urlParam.length());
                    if (downloadCredentialsFromS3(s3Link)) {
                        return;
                    }
                }
            }

            // Some QR code scanning apps url decode the provisioning link, so try that next:
            int urlStart = provisioningLink.indexOf(urlParam);
            if (urlStart >= 0) {
                String s3Link = provisioningLink.substring(urlStart + urlParam.length());
                if (downloadCredentialsFromS3(s3Link)) {
                    return;
                }
            }

            // Neither worked, show an error:
            runOnUiThread(() -> {
                AlertDialog alertDialog = new AlertDialog.Builder(MainActivity.this).create();
                alertDialog.setTitle("Error");
                alertDialog.setMessage("Invalid provisioning link");
                alertDialog.setButton(AlertDialog.BUTTON_NEUTRAL, "OK", (dialog, which) -> dialog.dismiss());
                alertDialog.show();
            });
        });
        t.start();
    }

    private boolean configureCredentials(InputStream inputStream) throws IOException {
        JsonReader reader = new JsonReader(new InputStreamReader(inputStream, StandardCharsets.UTF_8));
        String vehicleName = null;
        String endpointUrl = null;
        String certificate = null;
        String privateKey = null;
        String mqttTopicPrefix = "";
        reader.beginObject();
        while (reader.hasNext()) {
            switch (reader.nextName()) {
                case "vehicle_name":
                    vehicleName = reader.nextString();
                    break;
                case "endpoint_url":
                    endpointUrl = reader.nextString();
                    break;
                case "certificate":
                    certificate = reader.nextString();
                    break;
                case "private_key":
                    privateKey = reader.nextString();
                    break;
                case "mqtt_topic_prefix":
                    mqttTopicPrefix = reader.nextString();
                    break;
                default:
                    reader.skipValue();
                    break;
            }
        }
        reader.endObject();
        if (vehicleName != null && endpointUrl != null && certificate != null  && privateKey != null)
        {
            Log.i("configureCredentials", "Configured credentials for vehicle name "+vehicleName);
            SharedPreferences.Editor edit = mPrefs.edit();
            edit.putString("vehicle_name", vehicleName);
            edit.putString("mqtt_endpoint_url", endpointUrl);
            edit.putString("mqtt_certificate", certificate);
            edit.putString("mqtt_private_key", privateKey);
            edit.putString("mqtt_topic_prefix", mqttTopicPrefix);
            edit.apply();
            return true;
        }
        return false;
    }

    private boolean downloadCredentialsFromS3(String s3Link)
    {
        boolean res = false;
        try {
            Log.i("DownloadCredentials", "Trying to download from " + s3Link);
            URL url = new URL(s3Link);
            HttpURLConnection urlConnection = (HttpURLConnection) url.openConnection();
            try {
                res = configureCredentials(urlConnection.getInputStream());
            } finally {
                urlConnection.disconnect();
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
        return res;
    }

    Thread mStatusUpdateThread = new Thread(() -> {
        Log.i("MainActivity", "Status update thread started");
        try
        {
            while (true)
            {
                Thread.sleep(1000);
                String status = ((FweApplication)getApplication()).getStatusSummary();
                runOnUiThread(() -> findPreference("debug_status").setSummary(status));
            }
        }
        catch (InterruptedException ignored)
        {
        }
        Log.i("MainActivity", "Status update thread finished");
    });
}

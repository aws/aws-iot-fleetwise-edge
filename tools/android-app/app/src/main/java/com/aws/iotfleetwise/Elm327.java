// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

package com.aws.iotfleetwise;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;

public class Elm327 {
    public static final UUID SERIAL_PORT_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final int TIMEOUT_RESET_MS = 2000;
    private static final int TIMEOUT_SETUP_MS = 500;
    private static final int TIMEOUT_OBD_MS = 500;
    private static final int TIMEOUT_POLL_MS = 50;
    private static final String CMD_RESET = "AT Z";
    private static final String CMD_SET_PROTOCOL_AUTO = "AT SP 0";
    public static final String CMD_OBD_SUPPORTED_PIDS_0 = "01 00";

    private BluetoothSocket mSocket = null;
    private String mLastAddress = "";
    private String mStatus = "";

    void connect(String deviceParam)
    {
        if (!deviceParam.contains("\t")) {
            mStatus = "No device selected";
            return;
        }
        String[] deviceInfo = deviceParam.split("\t");
        String deviceAddress = deviceInfo[1];
        try {
            if (mSocket != null && mSocket.isConnected()) {
                if (mLastAddress.equals(deviceAddress)) {
                    return;
                }
                Log.i("ELM327.connect", "Closing connection to " + mLastAddress);
                mSocket.close();
            }
            Log.i("ELM327.connect", "Connecting to " + deviceAddress);
            BluetoothAdapter bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
            BluetoothDevice device = bluetoothAdapter.getRemoteDevice(deviceAddress);
            mSocket = device.createRfcommSocketToServiceRecord(SERIAL_PORT_UUID);
            mSocket.connect();
            mLastAddress = deviceAddress;

            Log.i("ELM327.connect", "Resetting device");
            List<String> res = sendCommand(CMD_RESET, TIMEOUT_RESET_MS);
            if (res == null) {
                throw new RuntimeException("Error resetting ELM327");
            }
            String version = res.get(0);

            Log.i("ELM327.connect", "Set automatic protocol");
            res = sendCommand(CMD_SET_PROTOCOL_AUTO, TIMEOUT_SETUP_MS);
            if (res == null) {
                throw new RuntimeException("Error setting ELM327 protocol");
            }

            mStatus = "Connected to "+version;
        }
        catch (SecurityException e) {
            mSocket = null;
            mStatus = "Access denied";
        }
        catch (Exception e) {
            mSocket = null;
            mStatus = "Connection error: "+e.getMessage();
        }
    }

    public String sendObdRequest(String request) {
        if (mSocket == null) {
            return "";
        }
        List<String> res = sendCommand(request, TIMEOUT_OBD_MS);
        if (res == null || res.size() == 0) {
            return "";
        }
        return res.get(0);
    }

    private List<String> sendCommand(String request, int timeout) {
        try {
            // Flush the input:
            receiveResponse(0, null);
            // Send the command:
            Log.i("ELM327.tx", request);
            OutputStream outputStream = mSocket.getOutputStream();
            outputStream.write((request + "\r").getBytes());
            outputStream.flush();
            // Receive the response:
            return receiveResponse(timeout, request);
        }
        catch (Exception e) {
            mSocket = null;
            mStatus = "Request error: "+e.getMessage();
            return null;
        }
    }

    private List<String> receiveResponse(int timeout, String request) throws IOException {
        InputStream inputStream = mSocket.getInputStream();
        List<String> result = new ArrayList<>();
        StringBuilder lineBuilder = new StringBuilder();
        int timeoutLeft = timeout;
        while (true) {
            if (inputStream.available() == 0) {
                if (timeoutLeft <= 0) {
                    if (timeout > 0) {
                        Log.e("ELM327.rx", "timeout");
                    }
                    return null;
                }
                try {
                    Thread.sleep(TIMEOUT_POLL_MS);
                    timeoutLeft -= TIMEOUT_POLL_MS;
                } catch (InterruptedException e) {
                    // Carry on
                }
                continue;
            }
            char chr = (char)inputStream.read();
            switch (chr) {
                case '>':
                    return result;
                case '\n':
                case '\r':
                    String line = lineBuilder.toString();
                    lineBuilder.setLength(0);
                    if (line.equals("?")) {
                        Log.e("ELM327.rx", line);
                        return null;
                    }
                    else if (line.equals("SEARCHING...")) {
                        Log.d("ELM327.rx", line);
                    }
                    else if (line.equals("") || line.equals(request)) {
                        // Ignore blank lines or the echoed command
                    }
                    else {
                        Log.i("ELM327.rx", line);
                        result.add(line);
                    }
                    break;
                default:
                    lineBuilder.append(chr);
                    break;
            }
        }
    }

    public String getStatus()
    {
        return mStatus;
    }
}

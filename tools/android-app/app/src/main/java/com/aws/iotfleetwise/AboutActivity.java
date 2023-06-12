// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

package com.aws.iotfleetwise;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.text.method.LinkMovementMethod;
import android.widget.EditText;
import android.widget.TextView;

import java.io.IOException;
import java.io.InputStream;

public class AboutActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_about);
        setResult(Activity.RESULT_CANCELED);

        TextView githubTextView = findViewById(R.id.github);
        githubTextView.setMovementMethod(LinkMovementMethod.getInstance());
        githubTextView.setLinkTextColor(Color.BLUE);

        try {
            InputStream stream = getAssets().open("THIRD-PARTY-LICENSES");
            int size = stream.available();
            byte[] buffer = new byte[size];
            stream.read(buffer);
            stream.close();
            String licenses = new String(buffer);
            EditText editText = findViewById(R.id.licenses);
            editText.setText(licenses);
        } catch (IOException ignored) {
        }
    }
}

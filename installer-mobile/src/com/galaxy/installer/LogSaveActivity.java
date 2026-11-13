package com.galaxy.installer;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Saves the debug log to a place the player picks with the system's
 * "save as" screen (a NativeActivity cannot receive the screen's answer, so
 * this small see-through activity asks for it).  Nothing is sent anywhere:
 * the player shares the saved file as they please.
 */
public class LogSaveActivity extends Activity {
    private static final String TAG = "LogSaveActivity";
    private static final int REQ_SAVE_LOG = 2001;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (savedInstanceState != null) {
            return;  // the "save as" screen is already up
        }
        try {
            String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("text/plain");
            intent.putExtra(Intent.EXTRA_TITLE, "galaxy_android_log_" + stamp + ".txt");
            startActivityForResult(intent, REQ_SAVE_LOG);
        } catch (Exception e) {
            Log.e(TAG, "No \"save as\" screen on this device", e);
            Toast.makeText(this, "Cannot open the save screen: " + e.getMessage(), Toast.LENGTH_LONG).show();
            finish();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_SAVE_LOG) {
            return;
        }
        final Uri target = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (target == null) {
            finish();  // cancelled
            return;
        }
        new Thread(() -> {
            String message;
            try {
                File log = AppInstaller.collectLogFile(this);
                long total = 0;
                try (InputStream in = new FileInputStream(log);
                     OutputStream out = getContentResolver().openOutputStream(target, "w")) {
                    if (out == null) {
                        throw new java.io.IOException("cannot write to the chosen place");
                    }
                    byte[] buf = new byte[65536];
                    int len;
                    while ((len = in.read(buf)) > 0) {
                        out.write(buf, 0, len);
                        total += len;
                    }
                }
                log.delete();
                message = "Debug log saved (" + Math.max(1, total / 1024) + " KB)";
            } catch (Exception e) {
                Log.e(TAG, "Failed to save the log", e);
                message = "Failed to save log: " + e.getMessage();
            }
            final String shown = message;
            runOnUiThread(() -> {
                Toast.makeText(this, shown, Toast.LENGTH_LONG).show();
                finish();
            });
        }).start();
    }
}

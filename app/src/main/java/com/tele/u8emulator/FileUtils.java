package com.tele.u8emulator;

import android.app.Activity;
import android.content.ContentResolver;
import android.net.Uri;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class FileUtils {
    private static final String TAG = "FileUtils";
    public static boolean writeToUri(Activity activity, Uri uri, byte[] data) {
        try {
            ContentResolver resolver = activity.getContentResolver();
            try (OutputStream stream = resolver.openOutputStream(uri, "wt")) {
                if (stream != null) {
                    stream.write(data);
                    stream.flush();
                    return true;
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
        return false;
    }

    public static byte[] readFromUri(Activity activity, Uri uri) {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        int totalBytesRead = 0;
        
        try {
            ContentResolver resolver = activity.getContentResolver();
            try (InputStream inputStream = resolver.openInputStream(uri)) {
                if (inputStream == null) {
                    Log.e(TAG, "Failed to open input stream");
                    return null;
                }

                byte[] data = new byte[16384];
                int nRead;
                while ((nRead = inputStream.read(data, 0, data.length)) != -1) {
                    buffer.write(data, 0, nRead);
                    totalBytesRead += nRead;
                }
                
                Log.d(TAG, "Successfully read " + totalBytesRead + " bytes");
                
                if (totalBytesRead == 0) {
                    Log.w(TAG, "Read 0 bytes from file");
                    return null;
                }
                
                return buffer.toByteArray();
            }
        } catch (IOException e) {
            Log.e(TAG, "Error reading file: " + e.getMessage());
            e.printStackTrace();
        } finally {
            try {
                buffer.close();
            } catch (IOException e) {
                Log.e(TAG, "Error closing buffer: " + e.getMessage());
            }
        }
        return null;
    }
}
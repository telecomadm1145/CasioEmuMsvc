package com.tele.u8emulator;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.Log;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import java.util.ArrayList;
import java.util.List;

public class PermissionManager {
    private static final String TAG = "PermissionManager";
    public static final int PERMISSION_REQUEST_CODE = 123;

    public static boolean checkAndRequestPermissions(Activity activity) {
        // For Android 10 and above, we don't need to request storage permissions as we'll use the SAF
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return true;
        }
        else {
            List<String> permissionsNeeded = new ArrayList<>();

            String[] requiredPermissions = {
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            };
            
            for (String permission : requiredPermissions) {
                if (ContextCompat.checkSelfPermission(activity, permission) 
                    != PackageManager.PERMISSION_GRANTED) {
                    permissionsNeeded.add(permission);
                }
            }

            if (!permissionsNeeded.isEmpty()) {
                Log.d(TAG, "Requesting standard storage permissions");
                ActivityCompat.requestPermissions(activity,
                    permissionsNeeded.toArray(new String[0]),
                    PERMISSION_REQUEST_CODE);
                return false;
            }
            
            return true;
        }
    }

    public static boolean hasAllPermissions(Activity activity) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            // For Android 10+, we don't need to check permissions as we'll use the SAF
            return true;
        } else {
            String[] requiredPermissions = {
                Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE
            };
            
            for (String permission : requiredPermissions) {
                if (ContextCompat.checkSelfPermission(activity, permission) 
                    != PackageManager.PERMISSION_GRANTED) {
                    return false;
                }
            }
            return true;
        }
    }
}
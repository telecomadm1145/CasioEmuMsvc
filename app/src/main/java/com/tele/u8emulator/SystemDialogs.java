package com.tele.u8emulator;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;

public class SystemDialogs {
    public static void openFileDialog(Activity activity) {
        if (!PermissionManager.checkAndRequestPermissions(activity)) {
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        activity.startActivityForResult(intent, 1);
    }

    public static void saveFileDialog(Activity activity, String preferredName) {
        if (!PermissionManager.checkAndRequestPermissions(activity)) {
            return;
        }
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_TITLE, preferredName);
        intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        activity.startActivityForResult(intent, 2);
    }

    public static void openFolderDialog(Activity activity) {
        if (!PermissionManager.checkAndRequestPermissions(activity)) {
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        activity.startActivityForResult(intent, 3);
    }

    public static void saveFolderDialog(Activity activity) {
        if (!PermissionManager.checkAndRequestPermissions(activity)) {
            return;
        }
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        activity.startActivityForResult(intent, 4);
    }
}
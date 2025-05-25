package com.tele.u8emulator;

import org.libsdl.app.SDLActivity;
import android.os.Vibrator;
import android.os.Environment;
import android.os.Build;
import android.app.Activity;
import android.net.Uri;
import android.database.Cursor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.MediaStore;
import android.content.pm.PackageManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.media.MediaScannerConnection;
import android.util.Log;
import android.graphics.Bitmap;
import android.widget.Toast;
import java.io.IOException;
import java.io.OutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;

public class Game extends SDLActivity {
    private static final String TAG = "Game";
    private static Uri pendingUri = null;
    private static byte[] pendingData = null;
    private static int pendingRequestCode = -1;
    
    private static native void onFileSelected(String path, byte[] data);
    private static native void onFileSaved(String path);
    private static native void onFolderSelected(String path);
    private static native void onFolderSaved(String path);
    private static native void onExportFailed();
    private static native void onImportFailed();

    public void vibrate(long milliseconds) {
        Vibrator vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (vibrator != null && vibrator.hasVibrator()) {
            vibrator.vibrate(milliseconds);
        }
    }

    public static void nativeVibrate(long milliseconds) {
        ((Game) SDLActivity.mSingleton).vibrate(milliseconds);
    }

    private String getPathFromUri(Uri uri) {
        String path = uri.toString();
        if (DocumentsContract.isDocumentUri(this, uri)) {
            try (Cursor cursor = getContentResolver().query(
                    uri,
                    new String[]{OpenableColumns.DISPLAY_NAME},
                    null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int columnIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (columnIndex != -1) {
                        path = cursor.getString(columnIndex);
                    }
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
        return path;
    }

    public static void exportData(byte[] data, Uri uri) {
        Activity activity = SDLActivity.mSingleton;
        if (activity == null) return;

        Game game = (Game)activity;
        if (!PermissionManager.hasAllPermissions(game)) {
            pendingUri = uri;
            pendingData = data;
            PermissionManager.checkAndRequestPermissions(game);
            return;
        }

        boolean success = FileUtils.writeToUri(activity, uri, data);
        if (!success) {
            onExportFailed();
        }
    }

    public boolean saveImageToMediaStore(ByteBuffer buffer, int width, int height, int pitch, String filename) {
        // Processing for Android 10+
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
            ContentValues values = new ContentValues();
            values.put(MediaStore.Images.Media.DISPLAY_NAME, filename);
            values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
            values.put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/CasioEmuAndroid");
            values.put(MediaStore.Images.Media.IS_PENDING, 1);
            
            ContentResolver resolver = getContentResolver();
            Uri imageUri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
            
            if (imageUri == null) {
                Log.e("SDL", "Failed to create new MediaStore record.");
                return false;
            }
            
            try {
                OutputStream stream = resolver.openOutputStream(imageUri);
                if (stream == null) {
                    Log.e("SDL", "Failed to open output stream.");
                    return false;
                }
                
                // Create a bitmap from the buffer
                Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
                buffer.rewind();
                bitmap.copyPixelsFromBuffer(buffer);
                
                // Compress and save the bitmap
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream);
                stream.close();
                
                values.clear();
                values.put(MediaStore.Images.Media.IS_PENDING, 0);
                resolver.update(imageUri, values, null, null);
                
                runOnUiThread(() -> Toast.makeText(this, "Screenshot saved to Pictures/CasioEmuAndroid folder", Toast.LENGTH_SHORT).show());
                
                return true;
            } catch (IOException e) {
                Log.e("SDL", "Error saving bitmap: " + e.getMessage());
                resolver.delete(imageUri, null, null);
                return false;
            }
        } 
        // Handling for Android 9 and lower
        else {
            try {
                Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
                buffer.rewind();
                bitmap.copyPixelsFromBuffer(buffer);
                
                File picturesDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_PICTURES);
                File casioDir = new File(picturesDir, "CasioEmuAndroid");
                
                if (!casioDir.exists()) {
                    boolean dirCreated = casioDir.mkdirs();
                    if (!dirCreated) {
                        Log.e("SDL", "Failed to create directory: " + casioDir.getAbsolutePath());
                        casioDir = picturesDir;
                    }
                }
                
                File imageFile = new File(casioDir, filename);
                FileOutputStream fos = new FileOutputStream(imageFile);
                
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, fos);
                fos.flush();
                fos.close();

                MediaScannerConnection.scanFile(this,
                        new String[] { imageFile.getAbsolutePath() }, null,
                        (path, uri) -> {
                            Log.d("SDL", "Scanned: " + path);
                            Log.d("SDL", "Uri: " + uri);
                        });
                
                runOnUiThread(() -> Toast.makeText(this, "Screenshot saved to Pictures/CasioEmuAndroid folder", Toast.LENGTH_SHORT).show());
                
                return true;
            } catch (IOException e) {
                Log.e("SDL", "Error saving bitmap: " + e.getMessage());
                return false;
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                         String[] permissions,
                                         int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        
        if (requestCode == PermissionManager.PERMISSION_REQUEST_CODE) {
            boolean allGranted = true;
            for (int result : grantResults) {
                if (result != PackageManager.PERMISSION_GRANTED) {
                    allGranted = false;
                    break;
                }
            }

            if (allGranted) {
                processPendingOperations();
            } else {
                onExportFailed();
            }
        }
    }

    private void processPendingOperations() {
        if (pendingUri != null && pendingData != null) {
            exportData(pendingData, pendingUri);
        } else if (pendingRequestCode != -1) {
            handlePendingRequest();
        }
        
        pendingUri = null;
        pendingData = null;
        pendingRequestCode = -1;
    }

    private void handlePendingRequest() {
        if (pendingRequestCode != -1 && pendingUri != null) {
            String path = getPathFromUri(pendingUri);
            
            switch (pendingRequestCode) {
                case 1: // Open File
                    byte[] fileData = FileUtils.readFromUri(this, pendingUri);
                    if (fileData != null) {
                        onFileSelected(path, fileData);
                    } else {
                        onImportFailed();
                    }
                    break;
                case 2: // Save File
                    onFileSaved(pendingUri.toString());
                    break;
                case 3: // Open Folder
                    onFolderSelected(path);
                    break;
                case 4: // Save Folder
                    onFolderSaved(path);
                    break;
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        
        if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
            Uri uri = data.getData();
            Log.d(TAG, "File URI: " + uri.toString());

            try {
                final int takeFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                    Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
                getContentResolver().takePersistableUriPermission(uri, takeFlags);
            } catch (Exception e) {
                Log.e(TAG, "Failed to take persistable permission: " + e.getMessage());
            }

            if (!PermissionManager.hasAllPermissions(this)) {
                pendingUri = uri;
                pendingRequestCode = requestCode;
                PermissionManager.checkAndRequestPermissions(this);
                return;
            }

            String path = getPathFromUri(uri);
            Log.d(TAG, "File path: " + path);
            
            switch (requestCode) {
                case 1: // Open File
                    byte[] fileData = FileUtils.readFromUri(this, uri);
                    if (fileData != null && fileData.length > 0) {
                        Log.d(TAG, "Read file data: " + fileData.length + " bytes");
                        onFileSelected(path, fileData);
                    } else {
                        Log.e(TAG, "Failed to read file data");
                        onImportFailed();
                    }
                    break;
                case 2: // Save File
                    onFileSaved(uri.toString());
                    break;
                case 3: // Open Folder
                    onFolderSelected(path);
                    break;
                case 4: // Save Folder
                    onFolderSaved(path);
                    break;
            }
        } else {
            Log.w(TAG, "Activity result failed or no data");
        }
    }
}
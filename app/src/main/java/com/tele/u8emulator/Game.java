package com.tele.u8emulator;

import org.libsdl.app.SDLActivity;
import android.os.Vibrator;
import android.os.Environment;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.app.Activity;
import android.net.Uri;
import android.database.Cursor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.MediaStore;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.ShortcutInfo;
import android.content.pm.ShortcutManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.graphics.BitmapFactory;
import android.graphics.drawable.Icon;
import android.media.MediaScannerConnection;
import android.util.Log;
import android.graphics.Bitmap;
import android.widget.Toast;
import android.app.AlertDialog;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.DialogInterface;
import android.system.Os;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Notification;
import android.os.Handler;
import android.os.Looper;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.ArrayList;
import java.util.zip.ZipFile;
import java.util.zip.ZipEntry;
import java.util.LinkedHashSet;
import java.util.Enumeration;
import io.sentry.Sentry;

public class Game extends SDLActivity {
    private static final String TAG = "Game";
    private static Uri pendingUri = null;
    private static byte[] pendingData = null;
    private static int pendingRequestCode = -1;
    private static final String CHANNEL_ID = "emu_channel";

    private Handler backgroundHandler = new Handler(Looper.getMainLooper());
    private Runnable stopEmulationRunnable = new Runnable() {
        @Override
        public void run() {
            NotificationCompat.Builder builder = new NotificationCompat.Builder(Game.this, CHANNEL_ID)
                    .setSmallIcon(android.R.drawable.ic_media_pause)
                    .setContentTitle("Emulation Stopped")
                    .setContentText("Emulation was stopped after 5 minutes in background.")
                    .setPriority(NotificationCompat.PRIORITY_HIGH)
                    .setAutoCancel(true);

            NotificationManagerCompat notificationManager = NotificationManagerCompat.from(Game.this);
            if (Build.VERSION.SDK_INT >= 33) {
                if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
                    notificationManager.notify(101, builder.build());
                }
            } else {
                notificationManager.notify(101, builder.build());
            }

            finish();
            System.exit(0);
        }
    };

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            CharSequence name = "Emulation Status";
            String description = "Notifications for emulator background running status";
            int importance = NotificationManager.IMPORTANCE_DEFAULT;
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID, name, importance);
            channel.setDescription(description);
            NotificationManager notificationManager = getSystemService(NotificationManager.class);
            if (notificationManager != null) {
                notificationManager.createNotificationChannel(channel);
            }
        }
    }

    private void setImmersiveMode() {
        if (Build.VERSION.SDK_INT >= 19) {
            View decorView = getWindow().getDecorView();
            int flags = View.SYSTEM_UI_FLAG_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE;

            decorView.setSystemUiVisibility(flags);
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;
            getWindow().setAttributes(attributes);
        }
        SDLActivity.onNativeResize();
    }

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        if (intent != null) {
            String modelPath = intent.getStringExtra("model_path");
            if (modelPath != null && !modelPath.isEmpty()) {
                return new String[]{ modelPath };
            }
        }
        return new String[0];
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        createNotificationChannel();
        setImmersiveMode();
        extractAssets();
        checkAndExtractPluginAssets();
        try {
            Os.setenv("TMPDIR", getCacheDir().getAbsolutePath(), true);
        } catch (Exception e) {}
    }

    @Override
    protected void onResume() {
        super.onResume();
        backgroundHandler.removeCallbacks(stopEmulationRunnable);
        NotificationManagerCompat notificationManager = NotificationManagerCompat.from(this);
        notificationManager.cancel(100);

        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS}, 102);
            }
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setContentTitle("Emulation Running")
                .setContentText("Emulation is currently running in the background.")
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setOngoing(true);

        NotificationManagerCompat notificationManager = NotificationManagerCompat.from(this);
        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
                notificationManager.notify(100, builder.build());
            }
        } else {
            notificationManager.notify(100, builder.build());
        }

        // 5 minutes = 5 * 60 * 1000 = 300,000 ms
        backgroundHandler.postDelayed(stopEmulationRunnable, 300000);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        backgroundHandler.removeCallbacks(stopEmulationRunnable);
        NotificationManagerCompat notificationManager = NotificationManagerCompat.from(this);
        notificationManager.cancel(100);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            setImmersiveMode();
        }
    }
    
    private void extractAssets() {
        File externalDir = getExternalFilesDir(null);
        if (externalDir == null) return;
        try {
            File romsDb = new File(externalDir, "roms.db");
            if (!romsDb.exists()) {
                InputStream in = getAssets().open("roms.db");
                FileOutputStream out = new FileOutputStream(romsDb);
                byte[] buffer = new byte[8192];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                }
                in.close();
                out.close();
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to extract roms.db: " + e.getMessage());
        }

        try {
            File localesDir = new File(externalDir, "locales");
            if (!localesDir.exists()) {
                localesDir.mkdirs();
            }
            String[] locales = getAssets().list("locales");
            if (locales != null) {
                for (String locale : locales) {
                    File localeFile = new File(localesDir, locale);
                    if (!localeFile.exists()) {
                        InputStream in = getAssets().open("locales/" + locale);
                        FileOutputStream out = new FileOutputStream(localeFile);
                        byte[] buffer = new byte[8192];
                        int read;
                        while ((read = in.read(buffer)) != -1) {
                            out.write(buffer, 0, read);
                        }
                        in.close();
                        out.close();
                    }
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to extract locales: " + e.getMessage());
        }
    }

    public static void checkAndExtractPluginAssets() {
        Game activity = (Game) SDLActivity.mSingleton;
        PackageManager pm = activity.getPackageManager();
        Intent pluginIntent = new Intent("com.tele.u8emulator.PLUGIN");
        List<ResolveInfo> resolveInfos = pm.queryIntentActivities(pluginIntent, 0);
        File pluginsDir = new File(activity.getCacheDir(), "plugins_so");
        if (!pluginsDir.exists()) pluginsDir.mkdirs();
        File loadOrderFile = new File(pluginsDir, "load_order.txt");
        if (loadOrderFile.exists()) loadOrderFile.delete();
        File infoFile = new File(pluginsDir, "plugins_info.txt");
        LinkedHashSet<String> loadOrder = new LinkedHashSet<>();
        StringBuilder infoBuilder = new StringBuilder();

        for (ResolveInfo info : resolveInfos) {
            try {
                ApplicationInfo appInfo = pm.getApplicationInfo(info.activityInfo.packageName, PackageManager.GET_META_DATA);
                Bundle metaData = appInfo.metaData;
                ZipFile zipFile = new ZipFile(appInfo.sourceDir);
                String targetAbi = Build.SUPPORTED_ABIS[0];
                String libPrefix = "lib/" + targetAbi + "/";
                List<String> extractedSos = new ArrayList<>();
                Enumeration<? extends ZipEntry> entries = zipFile.entries();
                while (entries.hasMoreElements()) {
                    ZipEntry entry = entries.nextElement();
                    String name = entry.getName();
                    if (name.startsWith(libPrefix) && name.endsWith(".so")) {
                        String soName = new File(name).getName();
                        if (soName.contains("libc++_shared")) continue;
                        File outFile = new File(pluginsDir, soName);
                        InputStream in = zipFile.getInputStream(entry);
                        FileOutputStream out = new FileOutputStream(outFile);
                        byte[] buffer = new byte[8192];
                        int read;
                        while ((read = in.read(buffer)) != -1) out.write(buffer, 0, read);
                        in.close(); out.close();
                        extractedSos.add(soName);
                    }
                }
                zipFile.close();
                String pName = "Unknown Plugin", pAuthor = "Unknown", pVer = "1.0", pDesc = "No description";
                
                if (metaData != null) {
                    pName = metaData.getString("casioemu.plugin.name", pName);
                    pAuthor = metaData.getString("casioemu.plugin.author", pAuthor);
                    Object v = metaData.get("casioemu.plugin.version");
                    if (v != null) pVer = v.toString();
                    pDesc = metaData.getString("casioemu.plugin.desc", pDesc);
                    String initClassName = metaData.getString("casioemu.plugin.init_class");
                    if (initClassName != null && !initClassName.isEmpty()) {
                        Context pluginContext = activity.createPackageContext(appInfo.packageName, Context.CONTEXT_INCLUDE_CODE | Context.CONTEXT_IGNORE_SECURITY);
                        ClassLoader pluginLoader = pluginContext.getClassLoader();
                        Class<?> initClass = pluginLoader.loadClass(initClassName);
                        java.lang.reflect.Method initMethod = initClass.getMethod("init", Context.class, Context.class, String.class);
                        initMethod.invoke(null, activity, pluginContext, activity.getCacheDir().getAbsolutePath());
                    }
                    String deps = metaData.getString("casioemu.plugin.dependencies");
                    if (deps != null && !deps.isEmpty()) {
                        for (String dep : deps.split(",")) loadOrder.add(dep.trim());
                    }
                    String mainLib = metaData.getString("casioemu.plugin.main_lib");
                    if (mainLib != null && !mainLib.isEmpty()) {
                        loadOrder.add(mainLib.trim());
                    } else {
                        for (String so : extractedSos) loadOrder.add(so);
                    }
                } else {
                    for (String so : extractedSos) loadOrder.add(so);
                }
                infoBuilder.append("Name: ").append(pName).append("\n");
                infoBuilder.append("Author: ").append(pAuthor).append("\n");
                infoBuilder.append("Version: ").append(pVer).append("\n");
                infoBuilder.append("Description: ").append(pDesc).append("\n");
                infoBuilder.append("Package: ").append(appInfo.packageName).append("\n");
                infoBuilder.append("---\n");
                
            } catch (Exception e) {
                Log.e(TAG, "Error processing plugin: " + e.getMessage());
            }
        }
        
        try {
            FileOutputStream fos = new FileOutputStream(loadOrderFile);
            for (String so : loadOrder) {
                fos.write((so + "\n").getBytes());
            }
            fos.close();
            FileOutputStream fosInfo = new FileOutputStream(infoFile);
            fosInfo.write(infoBuilder.toString().getBytes());
            fosInfo.close();
            Os.setenv("CASIOEMU_PLUGINS_DIR", pluginsDir.getAbsolutePath(), true);
        } catch (Exception e) {}
    }

    public void onNativeCrash(String message) {
        Log.e(TAG, "Native crash: " + message);
        runOnUiThread(() -> {
            new AlertDialog.Builder(this)
                .setTitle("Crash Detected")
                .setMessage(message)
                .setPositiveButton("Copy", (dialog, which) -> {
                    ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                    ClipData clip = ClipData.newPlainText("Crash Log", message);
                    clipboard.setPrimaryClip(clip);
                    Toast.makeText(this, "Copied to clipboard", Toast.LENGTH_SHORT).show();
                    // Exit cleanly
                    System.exit(0);
                })
                .setNegativeButton("Close", (dialog, which) -> {
                     System.exit(0);
                })
                .setCancelable(false)
                .show();
        });
    }

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

    public static void createModelShortcut(String modelPath, String shortcutName, String iconPath) {
        Activity activity = SDLActivity.mSingleton;
        if (activity == null) {
            Log.e(TAG, "createModelShortcut: activity is null");
            return;
        }

        activity.runOnUiThread(() -> {
            try {
                Intent launchIntent = new Intent(activity, Game.class);
                launchIntent.setAction(Intent.ACTION_MAIN);
                launchIntent.putExtra("model_path", modelPath);
                launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    ShortcutManager shortcutManager = activity.getSystemService(ShortcutManager.class);
                    if (shortcutManager != null && shortcutManager.isRequestPinShortcutSupported()) {
                        Icon icon = null;
                        if (iconPath != null && !iconPath.isEmpty()) {
                            File iconFile = new File(iconPath);
                            if (iconFile.exists()) {
                                Bitmap bmp = BitmapFactory.decodeFile(iconPath);
                                if (bmp != null) {
                                    // Scale to reasonable size for shortcut icon
                                    Bitmap scaled = Bitmap.createScaledBitmap(bmp, 192, 192, true);
                                    icon = Icon.createWithBitmap(scaled);
                                    if (scaled != bmp) bmp.recycle();
                                }
                            }
                        }
                        if (icon == null) {
                            icon = Icon.createWithResource(activity, activity.getApplicationInfo().icon);
                        }

                        ShortcutInfo shortcutInfo = new ShortcutInfo.Builder(activity, "model_" + modelPath.hashCode())
                                .setShortLabel(shortcutName)
                                .setLongLabel(shortcutName)
                                .setIcon(icon)
                                .setIntent(launchIntent)
                                .build();

                        shortcutManager.requestPinShortcut(shortcutInfo, null);
                    } else {
                        Toast.makeText(activity, "Pinned shortcuts not supported", Toast.LENGTH_SHORT).show();
                    }
                } else {
                    // Fallback for pre-Oreo
                    Intent shortcutIntent = new Intent("com.android.launcher.action.INSTALL_SHORTCUT");
                    shortcutIntent.putExtra(Intent.EXTRA_SHORTCUT_NAME, shortcutName);
                    shortcutIntent.putExtra(Intent.EXTRA_SHORTCUT_INTENT, launchIntent);
                    shortcutIntent.putExtra("duplicate", false);

                    if (iconPath != null && !iconPath.isEmpty()) {
                        File iconFile = new File(iconPath);
                        if (iconFile.exists()) {
                            Bitmap bmp = BitmapFactory.decodeFile(iconPath);
                            if (bmp != null) {
                                shortcutIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON, bmp);
                            }
                        }
                    }
                    if (!shortcutIntent.hasExtra(Intent.EXTRA_SHORTCUT_ICON)) {
                        shortcutIntent.putExtra(Intent.EXTRA_SHORTCUT_ICON_RESOURCE,
                                Intent.ShortcutIconResource.fromContext(activity, activity.getApplicationInfo().icon));
                    }

                    activity.sendBroadcast(shortcutIntent);
                }

                Toast.makeText(activity, "Shortcut created: " + shortcutName, Toast.LENGTH_SHORT).show();
            } catch (Exception e) {
                Log.e(TAG, "Failed to create shortcut: " + e.getMessage());
                Toast.makeText(activity, "Failed to create shortcut", Toast.LENGTH_SHORT).show();
            }
        });
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
        } else if (requestCode == 102) {
            if (grantResults.length > 0 && grantResults[0] != PackageManager.PERMISSION_GRANTED) {
                Log.w(TAG, "Notification permission denied");
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
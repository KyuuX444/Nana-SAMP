package com.wardrumstudios.utils;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.Uri;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.view.WindowManager;

import java.io.File;
import java.util.Locale;

/**
 * Clean from-scratch JNI host bridge implementing every com/wardrumstudios/utils/WarMedia
 * method looked up by libGTASA.so (ARM64 2.10).
 */
public abstract class WarMedia extends WarGamepad {
    protected String mainObbPath = "";
    protected String patchObbPath = "";
    protected boolean keyboardVisible = false;

    public native void NativeNotifyNetworkChange(boolean available);
    public native void setTouchSenseFilepath(String filepath);

    public void ShowKeyboard(int show) {
        keyboardVisible = (show != 0);
    }

    public boolean IsKeyboardShown() {
        return keyboardVisible;
    }

    public void PlayMovie(String filename, float volume) {}
    public void PlayMovieInFile(String filename, float volume, int offset, int length) {}
    public void PlayMovieInWindow(String filename, int x, int y, int w, int h,
                                  float volume, int offset, int length, int looping, boolean force) {}
    public void StopMovie() {}
    public void MovieSetSkippable(boolean skippable) {}
    public int IsMoviePlaying() { return 0; }
    public void MovieKeepAspectRatio(boolean keep) {}
    public void MovieSetText(String text, boolean display, boolean centered) {}
    public void MovieDisplayText(boolean display) {}
    public void MovieClearText(boolean clear) {}
    public void MovieSetTextScale(int scale) {}

    public boolean DeleteFile(String filename) {
        if (filename == null || filename.isEmpty()) return false;
        File f = filename.startsWith("/") ? new File(filename) : new File(gameDataRoot, filename);
        return f.exists() && f.delete();
    }

    public boolean FileRename(String oldName, String newName, int flags) {
        if (oldName == null || newName == null) return false;
        File src = oldName.startsWith("/") ? new File(oldName) : new File(gameDataRoot, oldName);
        File dst = newName.startsWith("/") ? new File(newName) : new File(gameDataRoot, newName);
        return src.exists() && src.renameTo(dst);
    }

    public String FileGetArchiveName(int type) {
        // 1 = main OBB archive, 2 = patch OBB archive
        if (type == 1) return mainObbPath != null ? mainObbPath : "";
        if (type == 2) return patchObbPath != null ? patchObbPath : "";
        return "";
    }

    public int GetDeviceLocale() {
        String lang = Locale.getDefault().getLanguage();
        if ("fr".equals(lang)) return 1;
        if ("de".equals(lang)) return 2;
        if ("it".equals(lang)) return 3;
        if ("es".equals(lang)) return 4;
        if ("ru".equals(lang)) return 5;
        if ("ja".equals(lang)) return 6;
        return 0; // English default
    }

    public int GetDeviceType() {
        // Packed GPU/CPU performance tier hint for GTA SA Android
        return (4 << 8) | (Runtime.getRuntime().availableProcessors() << 4) | 3;
    }

    public int GetDeviceInfo(int infoType) {
        if (infoType == 0) return Runtime.getRuntime().availableProcessors();
        if (infoType == 1) return 1; // touch screen present
        return 0;
    }

    public String GetAndroidBuildinfo(int type) {
        switch (type) {
            case 0: return Build.MANUFACTURER;
            case 1: return Build.MODEL;
            case 2: return Build.VERSION.RELEASE;
            default: return Build.DEVICE;
        }
    }

    public String OBFU_GetDeviceID() {
        try {
            String id = Settings.Secure.getString(getContentResolver(), Settings.Secure.ANDROID_ID);
            return id != null ? id : "nana-samp-device";
        } catch (Throwable t) {
            return "nana-samp-device";
        }
    }

    public boolean IsAppInstalled(String pkg) {
        if (pkg == null || pkg.isEmpty()) return false;
        try {
            getPackageManager().getPackageInfo(pkg, 0);
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            return false;
        }
    }

    public void OpenLink(String url) {
        if (url == null || url.isEmpty()) return;
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (Throwable ignored) {}
    }

    public void LoadAllGamesFromCloud() {}
    public String LoadGameFromCloud(int slot, byte[] buffer) { return ""; }
    public void SaveGameToCloud(int slot, byte[] data, int size) {}
    public boolean IsCloudAvailable() { return false; }
    public boolean NewCloudSaveAvailable(int slot) { return false; }

    public int GetSpecialBuildType() { return 0; }
    public void SendStatEvent(String eventId, boolean timed) {}
    public void SendStatEvent(String eventId, String paramName, String paramValue, boolean timed) {}
    public void SendTimedStatEventEnd(String eventId) {}

    public int GetTotalMemory() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) return 2048;
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        return (int) Math.min(Integer.MAX_VALUE, mi.totalMem / (1024L * 1024L));
    }

    public int GetLowThreshhold() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) return 128;
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        return (int) Math.min(Integer.MAX_VALUE, mi.threshold / (1024L * 1024L));
    }

    public int GetAvailableMemory() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) return 1024;
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        return (int) Math.min(Integer.MAX_VALUE, mi.availMem / (1024L * 1024L));
    }

    public float GetScreenWidthInches() {
        DisplayMetrics dm = getResources().getDisplayMetrics();
        if (dm.xdpi <= 0) return 5.5f;
        return dm.widthPixels / dm.xdpi;
    }

    public String GetAppId() {
        return getPackageName();
    }

    public void ScreenSetWakeLock(final boolean enable) {
        runOnUiThread(() -> {
            if (enable) {
                getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            } else {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        });
    }

    public boolean ServiceAppCommand(String cmd, String args) { return false; }
    public int ServiceAppCommandValue(String cmd, String args) { return 0; }
    public boolean ServiceAppCommandInt(String cmd, int arg) { return false; }

    public boolean isNetworkAvailable() {
        try {
            ConnectivityManager cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) return true;
            NetworkInfo info = cm.getActiveNetworkInfo();
            return info != null && info.isConnected();
        } catch (Throwable t) {
            return true;
        }
    }

    public boolean isWiFiAvailable() {
        try {
            ConnectivityManager cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) return false;
            NetworkInfo info = cm.getActiveNetworkInfo();
            return info != null && info.isConnected() && info.getType() == ConnectivityManager.TYPE_WIFI;
        } catch (Throwable t) {
            return false;
        }
    }

    public boolean isTV() {
        return false;
    }

    public void CreateTextBox(int id, int x, int y, int w, int h) {}
    public boolean ConvertToBitmap(byte[] data, int length) { return false; }

    public void VibratePhone(int durationMs) {
        if (durationMs <= 0) return;
        try {
            Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
            if (v == null || !v.hasVibrator()) return;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                v.vibrate(VibrationEffect.createOneShot(durationMs, VibrationEffect.DEFAULT_AMPLITUDE));
            } else {
                v.vibrate(durationMs);
            }
        } catch (Throwable ignored) {}
    }

    public void VibratePhoneEffect(int effect) {
        VibratePhone(25);
    }
}

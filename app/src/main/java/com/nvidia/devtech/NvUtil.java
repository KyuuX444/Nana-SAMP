package com.nvidia.devtech;

import android.app.Activity;
import android.content.SharedPreferences;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Clean from-scratch JNI bridge matching the exact com/nvidia/devtech/NvUtil contract
 * expected by Rockstar's GTA San Andreas Android 2.00 ARM32 engine (libGTASA.so).
 *
 * IMPORTANT: libGTASA.so 2.00's NvUtilInit() populates StorageRootBuffer and
 * StorageBaseRootBuffer via getAppLocalValue("STORAGE_ROOT") and
 * getAppLocalValue("STORAGE_ROOT_BASE"). Both setParameter() and setAppLocalValue()
 * keep an in-memory map synchronized so native lookups never receive an empty root path.
 */
public class NvUtil {
    private static final NvUtil INSTANCE = new NvUtil();
    private final Map<String, String> parameters = new ConcurrentHashMap<>();
    private final Map<String, String> appLocalValues = new ConcurrentHashMap<>();
    private volatile Activity activity;

    private NvUtil() {}

    public static NvUtil getInstance() {
        return INSTANCE;
    }

    public void setActivity(Activity act) {
        this.activity = act;
    }

    public void setParameter(String key, String value) {
        if (key != null && value != null) {
            parameters.put(key, value);
        }
    }

    public String getParameter(String key) {
        if (key == null) return "";
        String v = parameters.get(key);
        if (v != null) return v;
        String local = appLocalValues.get(key);
        return local != null ? local : "";
    }

    public boolean hasAppLocalValue(String key) {
        if (key == null) return false;
        if (appLocalValues.containsKey(key) || parameters.containsKey(key)) {
            return true;
        }
        Activity act = this.activity;
        if (act == null) return false;
        try {
            SharedPreferences prefs = act.getPreferences(Activity.MODE_PRIVATE);
            return prefs.contains(key);
        } catch (Throwable ignored) {
            return false;
        }
    }

    public String getAppLocalValue(String key) {
        if (key == null) return "";
        String v = appLocalValues.get(key);
        if (v != null) return v;
        String p = parameters.get(key);
        if (p != null) return p;
        Activity act = this.activity;
        if (act == null) return "";
        try {
            SharedPreferences prefs = act.getPreferences(Activity.MODE_PRIVATE);
            return prefs.getString(key, "");
        } catch (Throwable ignored) {
            return "";
        }
    }

    public void setAppLocalValue(String key, String value) {
        if (key == null) return;
        String safe = value != null ? value : "";
        appLocalValues.put(key, safe);
        Activity act = this.activity;
        if (act != null) {
            try {
                SharedPreferences prefs = act.getPreferences(Activity.MODE_PRIVATE);
                prefs.edit().putString(key, safe).apply();
            } catch (Throwable ignored) {
            }
        }
    }
}

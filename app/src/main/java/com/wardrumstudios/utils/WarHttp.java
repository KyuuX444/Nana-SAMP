package com.wardrumstudios.utils;

/**
 * Clean from-scratch JNI host stub for com/wardrumstudios/utils/WarHttp
 * expected by WarHttpInit inside libGTASA.so.
 */
public class WarHttp {
    private boolean addLineFeeds = false;

    public WarHttp(WarBase base) {
    }

    public void AddHttpGetLineFeeds(boolean enable) {
        this.addLineFeeds = enable;
    }

    public String HttpGet(String url) {
        return "";
    }

    public String HttpPost(String url) {
        return "";
    }

    public byte[] HttpGetData(String url) {
        return new byte[0];
    }
}

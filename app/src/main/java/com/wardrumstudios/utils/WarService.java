package com.wardrumstudios.utils;

/**
 * Clean from-scratch JNI host stub for com/wardrumstudios/utils/WarService.
 */
public class WarService {
    public native void jniWarService();

    public void LocalPushNotification(int id, String title, String message) {}
    public void LocalPushNotificationCancel() {}
    public void LocalPushNotificationCancel(int id) {}
}

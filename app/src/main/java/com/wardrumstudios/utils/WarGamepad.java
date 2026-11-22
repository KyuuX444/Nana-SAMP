package com.wardrumstudios.utils;

import android.view.ViewParent;

/**
 * Clean from-scratch JNI host bridge for com/wardrumstudios/utils/WarGamepad.
 * Satisfies every JNI RegisterNatives & GetMethodID lookup performed by
 * WarGamepadInit / NVEventInit inside libGTASA.so 2.00 ARM32.
 */
public abstract class WarGamepad extends WarBase {
    // Registered via JNI RegisterNatives by libGTASA.so 2.00 (WarGamepad_ProcessTouchpadAsPointer)
    public native boolean processTouchpadAsPointer(ViewParent parent, boolean enable);

    public int GetGamepadType(int index) {
        return -1;
    }

    public int GetGamepadButtons(int index) {
        return 0;
    }

    public float GetGamepadAxis(int index, int axis) {
        return 0.0f;
    }

    public int GetGamepadTrack(int index, int axis, int flag) {
        return 0;
    }
}

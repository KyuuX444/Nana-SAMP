package com.wardrumstudios.utils;

import com.nvidia.devtech.NvEventQueueActivity;

/**
 * Base class in the JNI host hierarchy expected by libGTASA.so (WarHttp constructor takes WarBase).
 */
public abstract class WarBase extends NvEventQueueActivity {
}

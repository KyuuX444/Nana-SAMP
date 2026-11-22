package com.nvidia.devtech;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;

/**
 * Clean from-scratch EGL/GLES2 + input + lifecycle JNI host activity implementing the
 * exact com/nvidia/devtech/NvEventQueueActivity interface bound by libGTASA.so (ARM32 2.00).
 */
public abstract class NvEventQueueActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "NanaSAMP-NvActivity";
    private static final int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
    private static final int EGL_OPENGL_ES2_BIT = 4;

    public static class RawData {
        public byte[] data = new byte[0];
        public int length = 0;
    }

    public static class RawTexture extends RawData {
        public int width = 0;
        public int height = 0;
    }

    protected SurfaceView gameSurfaceView;
    protected SurfaceHolder surfaceHolder;
    protected boolean nativeEngineStarted = false;
    protected boolean nativeLibrariesLoaded = false;
    protected boolean supportPauseResume = true;
    protected int surfaceWidth = 0;
    protected int surfaceHeight = 0;
    protected String gameDataRoot = "";

    protected volatile boolean eglInitialized = false;
    protected volatile int eglSwapCount = 0;
    protected volatile String lastEglError = "";

    private EGL10 egl;
    private EGLDisplay eglDisplay = EGL10.EGL_NO_DISPLAY;
    private EGLContext eglContext = EGL10.EGL_NO_CONTEXT;
    private EGLSurface eglSurface = EGL10.EGL_NO_SURFACE;
    private EGLConfig eglConfig;
    private final Object eglLock = new Object();
    private boolean engineResumedOnce = false;

    // Exact 13 RegisterNatives + jniNvAPKInit bound by libGTASA.so 2.00 JNI_OnLoad
    public native boolean init(boolean hasSurface);
    public native boolean touchEvent(int action, int x, int y, MotionEvent event);
    public native boolean multiTouchEvent(int action, int count, int x0, int y0, int x1, int y1, MotionEvent event);
    public native boolean multiTouchEvent4(
            int action, int count,
            int x0, int y0, int x1, int y1,
            int x2, int y2, int x3, int y3,
            MotionEvent event);
    public native boolean keyEvent(int action, int keycode, int unicodeChar, int metaState, KeyEvent event);
    public native boolean accelerometerEvent(float x, float y, float z);
    public native void cleanup();
    public native void setWindowSize(int width, int height);
    public native void quitAndWait();
    public native void resumeEvent();
    public native void pauseEvent();
    public native void imeClosed();
    public native void lowMemoryEvent();
    public native void jniNvAPKInit(Object assetManager);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        NvUtil.getInstance().setActivity(this);
    }

    protected void attachGameSurfaceView(SurfaceView view) {
        this.gameSurfaceView = view;
        SurfaceHolder holder = view.getHolder();
        holder.setFormat(PixelFormat.RGBA_8888);
        holder.addCallback(this);
        this.surfaceHolder = holder;
    }

    protected void startNativeGameIfReady() {
        if (!nativeLibrariesLoaded || nativeEngineStarted || surfaceHolder == null) {
            return;
        }
        // Wait for valid non-zero surface dimensions before launching libGTASA.so render thread
        if (surfaceWidth <= 0 || surfaceHeight <= 0) {
            return;
        }
        Surface s = surfaceHolder.getSurface();
        if (s == null || !s.isValid()) {
            return;
        }
        try {
            jniNvAPKInit(getAssets());
        } catch (Throwable t) {
            Log.e(TAG, "jniNvAPKInit failed", t);
        }
        try {
            // 1. Initialize libGTASA.so 2.00 NVEvent queue, NvUtilInit(), and spawn NVEventAppMain thread.
            //    Note: libGTASA.so 2.00 initializes IsAndroidPaused = 1 in .data, so NVEventAppMain
            //    blocks inside ProcessEvents() until NV_EVENT_RESUME (resumeEvent()) is enqueued!
            nativeEngineStarted = init(true);
            if (nativeEngineStarted) {
                // 2. Push valid window dimensions AFTER init() creates the NVEvent queue,
                //    then push resumeEvent() to clear IsAndroidPaused=1 and unblock OS_ApplicationStartup.
                setWindowSize(surfaceWidth, surfaceHeight);
                resumeEvent();
                engineResumedOnce = true;
            }
            Log.i(TAG, "Native GTA SA 2.00 engine started=" + nativeEngineStarted +
                    " resumed=" + engineResumedOnce +
                    " (" + surfaceWidth + "x" + surfaceHeight + ")");
        } catch (Throwable t) {
            Log.e(TAG, "Failed to start native GTA SA engine", t);
            lastEglError = "Native init failed: " + t.getMessage();
            nativeEngineStarted = false;
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        this.surfaceHolder = holder;
        if (nativeEngineStarted && nativeLibrariesLoaded && engineResumedOnce) {
            try {
                if (surfaceWidth > 0 && surfaceHeight > 0) {
                    setWindowSize(surfaceWidth, surfaceHeight);
                }
                resumeEvent();
            } catch (Throwable t) {
                Log.w(TAG, "resumeEvent failed", t);
            }
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        this.surfaceHolder = holder;
        this.surfaceWidth = width;
        this.surfaceHeight = height;
        if (nativeLibrariesLoaded && width > 0 && height > 0) {
            if (!nativeEngineStarted) {
                startNativeGameIfReady();
            } else {
                try {
                    setWindowSize(width, height);
                    if (!engineResumedOnce) {
                        resumeEvent();
                        engineResumedOnce = true;
                    }
                } catch (Throwable t) {
                    Log.w(TAG, "setWindowSize failed", t);
                }
            }
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (nativeLibrariesLoaded && nativeEngineStarted) {
            try {
                pauseEvent();
            } catch (Throwable t) {
                Log.w(TAG, "pauseEvent failed", t);
            }
        }
        destroyEglSurfaceOnly();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (nativeLibrariesLoaded && nativeEngineStarted) {
            try {
                resumeEvent();
            } catch (Throwable t) {
                Log.w(TAG, "onResume resumeEvent failed", t);
            }
        }
    }

    @Override
    protected void onPause() {
        if (nativeLibrariesLoaded && nativeEngineStarted) {
            try {
                pauseEvent();
            } catch (Throwable t) {
                Log.w(TAG, "onPause pauseEvent failed", t);
            }
        }
        super.onPause();
    }

    @Override
    public void onLowMemory() {
        super.onLowMemory();
        if (nativeLibrariesLoaded && nativeEngineStarted) {
            try {
                lowMemoryEvent();
            } catch (Throwable ignored) {
            }
        }
    }

    @Override
    protected void onDestroy() {
        if (nativeLibrariesLoaded && nativeEngineStarted) {
            try {
                quitAndWait();
            } catch (Throwable t) {
                Log.w(TAG, "quitAndWait exception", t);
            }
            nativeEngineStarted = false;
        }
        cleanupEgl();
        super.onDestroy();
    }

    public boolean forwardGameTouchEvent(MotionEvent event) {
        if (!nativeLibrariesLoaded || !nativeEngineStarted || event == null) {
            return false;
        }
        int action = event.getAction();
        int count = event.getPointerCount();
        if (count <= 1) {
            return touchEvent(action, (int) event.getX(0), (int) event.getY(0), event);
        }
        int x0 = (int) event.getX(0);
        int y0 = (int) event.getY(0);
        int x1 = (int) event.getX(1);
        int y1 = (int) event.getY(1);
        if (count == 2) {
            return multiTouchEvent(action, count, x0, y0, x1, y1, event);
        }
        int x2 = (int) event.getX(2);
        int y2 = (int) event.getY(2);
        int x3 = count > 3 ? (int) event.getX(3) : 0;
        int y3 = count > 3 ? (int) event.getY(3) : 0;
        return multiTouchEvent4(action, Math.min(count, 4), x0, y0, x1, y1, x2, y2, x3, y3, event);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (nativeLibrariesLoaded && nativeEngineStarted &&
                keyCode != KeyEvent.KEYCODE_VOLUME_UP &&
                keyCode != KeyEvent.KEYCODE_VOLUME_DOWN &&
                keyCode != KeyEvent.KEYCODE_BACK) {
            if (keyEvent(event.getAction(), keyCode, event.getUnicodeChar(), event.getMetaState(), event)) {
                return true;
            }
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (nativeLibrariesLoaded && nativeEngineStarted &&
                keyCode != KeyEvent.KEYCODE_VOLUME_UP &&
                keyCode != KeyEvent.KEYCODE_VOLUME_DOWN &&
                keyCode != KeyEvent.KEYCODE_BACK) {
            if (keyEvent(event.getAction(), keyCode, event.getUnicodeChar(), event.getMetaState(), event)) {
                return true;
            }
        }
        return super.onKeyUp(keyCode, event);
    }

    // ---- Methods invoked by libGTASA.so via JNI ----

    public boolean getSupportPauseResume() {
        return supportPauseResume;
    }

    public int getOrientation() {
        try {
            return getWindowManager().getDefaultDisplay().getRotation();
        } catch (Throwable t) {
            return 0;
        }
    }

    private EGLConfig chooseBestEglConfig(EGL10 egl, EGLDisplay display) {
        int[][] specs = new int[][] {
                {
                        EGL10.EGL_SURFACE_TYPE, EGL10.EGL_WINDOW_BIT,
                        EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                        EGL10.EGL_RED_SIZE, 8,
                        EGL10.EGL_GREEN_SIZE, 8,
                        EGL10.EGL_BLUE_SIZE, 8,
                        EGL10.EGL_ALPHA_SIZE, 8,
                        EGL10.EGL_DEPTH_SIZE, 24,
                        EGL10.EGL_STENCIL_SIZE, 8,
                        EGL10.EGL_NONE
                },
                {
                        EGL10.EGL_SURFACE_TYPE, EGL10.EGL_WINDOW_BIT,
                        EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                        EGL10.EGL_RED_SIZE, 8,
                        EGL10.EGL_GREEN_SIZE, 8,
                        EGL10.EGL_BLUE_SIZE, 8,
                        EGL10.EGL_ALPHA_SIZE, 8,
                        EGL10.EGL_DEPTH_SIZE, 16,
                        EGL10.EGL_STENCIL_SIZE, 8,
                        EGL10.EGL_NONE
                },
                {
                        EGL10.EGL_SURFACE_TYPE, EGL10.EGL_WINDOW_BIT,
                        EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                        EGL10.EGL_RED_SIZE, 8,
                        EGL10.EGL_GREEN_SIZE, 8,
                        EGL10.EGL_BLUE_SIZE, 8,
                        EGL10.EGL_DEPTH_SIZE, 16,
                        EGL10.EGL_NONE
                },
                {
                        EGL10.EGL_SURFACE_TYPE, EGL10.EGL_WINDOW_BIT,
                        EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                        EGL10.EGL_RED_SIZE, 5,
                        EGL10.EGL_GREEN_SIZE, 6,
                        EGL10.EGL_BLUE_SIZE, 5,
                        EGL10.EGL_DEPTH_SIZE, 16,
                        EGL10.EGL_NONE
                },
                {
                        EGL10.EGL_SURFACE_TYPE, EGL10.EGL_WINDOW_BIT,
                        EGL10.EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                        EGL10.EGL_NONE
                }
        };
        EGLConfig[] configs = new EGLConfig[1];
        int[] numConfig = new int[1];
        for (int[] spec : specs) {
            if (egl.eglChooseConfig(display, spec, configs, 1, numConfig) &&
                    numConfig[0] > 0 && configs[0] != null) {
                return configs[0];
            }
        }
        return null;
    }

    public boolean InitEGLAndGLES2(int eglClientVersion) {
        synchronized (eglLock) {
            try {
                if (egl == null) {
                    egl = (EGL10) EGLContext.getEGL();
                }
                if (eglDisplay == EGL10.EGL_NO_DISPLAY) {
                    eglDisplay = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
                    int[] version = new int[2];
                    if (eglDisplay == EGL10.EGL_NO_DISPLAY || !egl.eglInitialize(eglDisplay, version)) {
                        lastEglError = "eglInitialize failed (err=" + egl.eglGetError() + ")";
                        Log.e(TAG, lastEglError);
                        return false;
                    }
                }
                if (eglConfig == null) {
                    eglConfig = chooseBestEglConfig(egl, eglDisplay);
                    if (eglConfig == null) {
                        lastEglError = "eglChooseConfig failed for all fallback tiers";
                        Log.e(TAG, lastEglError);
                        return false;
                    }
                }
                if (eglContext == EGL10.EGL_NO_CONTEXT) {
                    int reqVer = Math.max(2, eglClientVersion);
                    int[] contextAttribs = new int[] {
                            EGL_CONTEXT_CLIENT_VERSION, reqVer,
                            EGL10.EGL_NONE
                    };
                    eglContext = egl.eglCreateContext(eglDisplay, eglConfig, EGL10.EGL_NO_CONTEXT, contextAttribs);
                    if ((eglContext == null || eglContext == EGL10.EGL_NO_CONTEXT) && reqVer > 2) {
                        int[] es2Attribs = new int[] { EGL_CONTEXT_CLIENT_VERSION, 2, EGL10.EGL_NONE };
                        eglContext = egl.eglCreateContext(eglDisplay, eglConfig, EGL10.EGL_NO_CONTEXT, es2Attribs);
                    }
                    if (eglContext == null || eglContext == EGL10.EGL_NO_CONTEXT) {
                        lastEglError = "eglCreateContext failed (err=" + egl.eglGetError() + ")";
                        Log.e(TAG, lastEglError);
                        return false;
                    }
                }
                if (eglSurface == EGL10.EGL_NO_SURFACE) {
                    if (surfaceHolder == null || surfaceHolder.getSurface() == null || !surfaceHolder.getSurface().isValid()) {
                        lastEglError = "SurfaceHolder invalid during InitEGLAndGLES2";
                        Log.e(TAG, lastEglError);
                        return false;
                    }
                    eglSurface = egl.eglCreateWindowSurface(eglDisplay, eglConfig, surfaceHolder, null);
                    if (eglSurface == null || eglSurface == EGL10.EGL_NO_SURFACE) {
                        lastEglError = "eglCreateWindowSurface failed (err=" + egl.eglGetError() + ")";
                        Log.e(TAG, lastEglError);
                        return false;
                    }
                }
                boolean bound = makeCurrent();
                if (bound) {
                    eglInitialized = true;
                    lastEglError = "";
                } else {
                    lastEglError = "eglMakeCurrent failed (err=" + egl.eglGetError() + ")";
                    Log.e(TAG, lastEglError);
                }
                return bound;
            } catch (Throwable t) {
                lastEglError = "InitEGLAndGLES2 error: " + t.getMessage();
                Log.e(TAG, lastEglError, t);
                return false;
            }
        }
    }

    public boolean makeCurrent() {
        synchronized (eglLock) {
            if (egl == null || eglDisplay == EGL10.EGL_NO_DISPLAY || eglContext == EGL10.EGL_NO_CONTEXT) {
                return false;
            }
            if (eglSurface == EGL10.EGL_NO_SURFACE && surfaceHolder != null &&
                    surfaceHolder.getSurface() != null && surfaceHolder.getSurface().isValid()) {
                eglSurface = egl.eglCreateWindowSurface(eglDisplay, eglConfig, surfaceHolder, null);
            }
            if (eglSurface == null || eglSurface == EGL10.EGL_NO_SURFACE) {
                return false;
            }
            return egl.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
        }
    }

    public boolean unMakeCurrent() {
        synchronized (eglLock) {
            if (egl == null || eglDisplay == EGL10.EGL_NO_DISPLAY) {
                return true;
            }
            return egl.eglMakeCurrent(eglDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
        }
    }

    public boolean swapBuffers() {
        synchronized (eglLock) {
            if (egl == null || eglDisplay == EGL10.EGL_NO_DISPLAY || eglSurface == EGL10.EGL_NO_SURFACE) {
                return false;
            }
            boolean ok = egl.eglSwapBuffers(eglDisplay, eglSurface);
            if (ok) {
                eglSwapCount++;
            }
            return ok;
        }
    }

    private void destroyEglSurfaceOnly() {
        synchronized (eglLock) {
            if (egl != null && eglDisplay != EGL10.EGL_NO_DISPLAY && eglSurface != EGL10.EGL_NO_SURFACE) {
                egl.eglMakeCurrent(eglDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
                egl.eglDestroySurface(eglDisplay, eglSurface);
                eglSurface = EGL10.EGL_NO_SURFACE;
            }
        }
    }

    private void cleanupEgl() {
        synchronized (eglLock) {
            if (egl != null && eglDisplay != EGL10.EGL_NO_DISPLAY) {
                egl.eglMakeCurrent(eglDisplay, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
                if (eglSurface != EGL10.EGL_NO_SURFACE) {
                    egl.eglDestroySurface(eglDisplay, eglSurface);
                    eglSurface = EGL10.EGL_NO_SURFACE;
                }
                if (eglContext != EGL10.EGL_NO_CONTEXT) {
                    egl.eglDestroyContext(eglDisplay, eglContext);
                    eglContext = EGL10.EGL_NO_CONTEXT;
                }
                egl.eglTerminate(eglDisplay);
                eglDisplay = EGL10.EGL_NO_DISPLAY;
            }
        }
    }

    /**
     * Never returns RawData with null `data` array because libGTASA.so 2.00 calls
     * JNI GetByteArrayElements(raw.data, NULL) without a null check.
     */
    public RawData loadFile(String filename) {
        RawData result = new RawData();
        if (filename == null || filename.isEmpty()) return result;

        InputStream is = null;
        try {
            if (gameDataRoot != null && !gameDataRoot.isEmpty()) {
                File f = new File(gameDataRoot, filename);
                if (f.exists() && f.isFile()) {
                    is = new FileInputStream(f);
                }
            }
            if (is == null) {
                is = getAssets().open(filename);
            }
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) > 0) {
                bos.write(buf, 0, n);
            }
            byte[] bytes = bos.toByteArray();
            result.data = bytes != null ? bytes : new byte[0];
            result.length = result.data.length;
        } catch (Throwable ignored) {
            result.data = new byte[0];
            result.length = 0;
        } finally {
            if (is != null) {
                try { is.close(); } catch (Throwable ignored) {}
            }
        }
        return result;
    }

    /**
     * Never returns RawTexture with null `data` array because libGTASA.so 2.00 calls
     * JNI GetByteArrayElements(tex.data, NULL) without a null check.
     */
    public RawTexture loadTexture(String filename) {
        RawTexture tex = new RawTexture();
        RawData raw = loadFile(filename);
        if (raw.data == null || raw.length == 0) {
            tex.width = 1;
            tex.height = 1;
            tex.data = new byte[] { 0, 0, 0, 0 };
            tex.length = 4;
            return tex;
        }
        Bitmap bmp = BitmapFactory.decodeByteArray(raw.data, 0, raw.length);
        if (bmp == null) {
            tex.width = 1;
            tex.height = 1;
            tex.data = new byte[] { 0, 0, 0, 0 };
            tex.length = 4;
            return tex;
        }
        int w = bmp.getWidth();
        int h = bmp.getHeight();
        int[] pixels = new int[w * h];
        bmp.getPixels(pixels, 0, w, 0, 0, w, h);
        bmp.recycle();

        byte[] rgba = new byte[w * h * 4];
        int outIdx = 0;
        for (int p : pixels) {
            rgba[outIdx++] = (byte) ((p >> 16) & 0xFF); // R
            rgba[outIdx++] = (byte) ((p >> 8) & 0xFF);  // G
            rgba[outIdx++] = (byte) (p & 0xFF);         // B
            rgba[outIdx++] = (byte) ((p >>> 24) & 0xFF);// A
        }
        tex.width = w;
        tex.height = h;
        tex.data = rgba;
        tex.length = rgba.length;
        return tex;
    }
}

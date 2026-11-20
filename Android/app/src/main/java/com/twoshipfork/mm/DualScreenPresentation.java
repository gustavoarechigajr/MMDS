package com.twoshipfork.mm;

import android.app.Presentation;
import android.os.Bundle;
import android.view.Display;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;

/**
 * Full-screen SurfaceView on the secondary display. Native code renders the second-screen panels
 * into it (see mm/2s2h/Enhancements/DualScreen) and receives touches through
 * {@link MainActivity#nativeDualScreenTouch}.
 */
public class DualScreenPresentation extends Presentation {
    private final MainActivity activity;

    private final SurfaceHolder.Callback surfaceCallback = new SurfaceHolder.Callback() {
        @Override
        public void surfaceCreated(SurfaceHolder holder) {
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            MainActivity.nativeDualScreenSurfaceChanged(holder.getSurface(), width, height);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            MainActivity.nativeDualScreenSurfaceDestroyed();
        }
    };

    public DualScreenPresentation(MainActivity activity, Display display) {
        super(activity, display);
        this.activity = activity;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Not focusable: touches still arrive, but controller input and focus stay with the game
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
                | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);

        SurfaceView surfaceView = new SurfaceView(getContext());
        surfaceView.getHolder().addCallback(surfaceCallback);
        surfaceView.setOnTouchListener((View v, MotionEvent e) -> {
            int w = Math.max(1, v.getWidth());
            int h = Math.max(1, v.getHeight());
            // The first two fingers, for pinch zoom; the second is only meaningful when pointers >= 2
            int pointers = e.getPointerCount();
            if (e.getActionMasked() == MotionEvent.ACTION_POINTER_UP) {
                pointers--; // The lifted finger is still counted in this event
            }
            float x2 = pointers >= 2 ? e.getX(1) / w : 0.0f;
            float y2 = pointers >= 2 ? e.getY(1) / h : 0.0f;
            MainActivity.nativeDualScreenTouch(e.getActionMasked(), e.getX(0) / w, e.getY(0) / h, x2, y2, pointers);
            return true;
        });
        setContentView(surfaceView);
        hideSystemBars();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        hideSystemBars();
    }

    private void hideSystemBars() {
        View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }
}

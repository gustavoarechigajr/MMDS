package com.twoshipfork.mm;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.util.Log;
import android.view.Display;

/**
 * Finds a secondary presentation display (e.g. the AYN Thor's bottom screen) and keeps a
 * {@link DualScreenPresentation} on it while the game is in the foreground.
 */
public class DualScreenManager {
    private static final String TAG = "DualScreenManager";

    private final MainActivity activity;
    private final DisplayManager displayManager;
    private DualScreenPresentation presentation;

    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override
        public void onDisplayAdded(int displayId) {
            Display display = displayManager.getDisplay(displayId);
            if (presentation == null && isPresentationDisplay(display)) {
                showPresentation(display);
            }
        }

        @Override
        public void onDisplayRemoved(int displayId) {
            if (presentation != null && presentation.getDisplay() != null
                    && presentation.getDisplay().getDisplayId() == displayId) {
                dismissPresentation();
            }
        }

        @Override
        public void onDisplayChanged(int displayId) {
        }
    };

    public DualScreenManager(MainActivity activity) {
        this.activity = activity;
        this.displayManager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
    }

    private static boolean isPresentationDisplay(Display display) {
        return display != null && display.getDisplayId() != Display.DEFAULT_DISPLAY
                && (display.getFlags() & Display.FLAG_PRESENTATION) != 0;
    }

    public void start() {
        if (displayManager == null) {
            return;
        }
        displayManager.registerDisplayListener(displayListener, null);
        if (presentation != null) {
            return;
        }
        for (Display display : displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (isPresentationDisplay(display)) {
                showPresentation(display);
                break;
            }
        }
    }

    public void stop() {
        if (displayManager != null) {
            displayManager.unregisterDisplayListener(displayListener);
        }
        dismissPresentation();
    }

    private void showPresentation(Display display) {
        dismissPresentation();
        try {
            presentation = new DualScreenPresentation(activity, display);
            presentation.show();
        } catch (Exception e) {
            Log.w(TAG, "Failed to show dual screen presentation", e);
            presentation = null;
        }
    }

    private void dismissPresentation() {
        if (presentation != null) {
            presentation.dismiss();
            presentation = null;
        }
    }
}

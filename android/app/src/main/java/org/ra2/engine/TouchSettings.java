package org.ra2.engine;
// Android fork additions by Bryle13; project 0BSD license.

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Clean, neat touch control mapping and fullscreen free-placement drag editor. */
final class TouchSettings {

    static class ActionItem {
        final String id;
        final String label;
        final String defaultHotkey;
        final int maskBit;
        boolean active;
        float xPct = -1f; // Free placement X percentage (0-100) or -1 for default dock
        float yPct = -1f; // Free placement Y percentage (0-100) or -1 for default dock

        ActionItem(String id, String label, String defaultHotkey, int maskBit, boolean active) {
            this.id = id;
            this.label = label;
            this.defaultHotkey = defaultHotkey;
            this.maskBit = maskBit;
            this.active = active;
        }
    }

    private static final List<ActionItem> ALL_ACTIONS = Arrays.asList(
            new ActionItem("STOP", "Stop", "S", 0, true),
            new ActionItem("GUARD", "Guard", "G", 1, true),
            new ActionItem("DEPLOY", "Deploy / Unload", "D", 2, true),
            new ActionItem("SCATTER", "Scatter", "X", 3, true),
            new ActionItem("FIRE", "Force Fire", "Ctrl", 4, true),
            new ActionItem("MOVE", "Attack Move", "Ctrl + Shift", 5, true),
            new ActionItem("ADD", "Add Selection", "Shift", 6, true),
            new ActionItem("BOX", "Box Select Mode", "Drag Box", -1, false),
            new ActionItem("ROUTE", "Waypoint Route Z", "Z", -1, false),
            new ActionItem("HOME", "Home Base View", "H", -1, false)
    );

    static void show(Context context) {
        SharedPreferences p = context.getSharedPreferences("interface", Context.MODE_PRIVATE);

        int mask = p.getInt("touch_actions", 127);
        String savedMapping = p.getString("touch_mapping", "");
        String savedPositions = p.getString("touch_positions", "");
        int savedScale = p.getInt("touch_scale", 100);
        int savedOpacity = p.getInt("touch_opacity", 100);
        boolean savedCollapsed = p.getBoolean("touch_collapsed", true);

        List<ActionItem> currentLayout = parseLayout(savedMapping, savedPositions, mask);

        float density = context.getResources().getDisplayMetrics().density;
        int dp8 = Math.round(8 * density);
        int dp12 = Math.round(12 * density);
        int dp16 = Math.round(16 * density);

        LinearLayout root = new LinearLayout(context);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp16, dp16, dp16, dp16);

        TextView subtitle = new TextView(context);
        subtitle.setText("Customize touch buttons, scale, opacity, or open the Fullscreen Touch Position Editor to drag buttons anywhere on screen.");
        subtitle.setTextSize(13);
        subtitle.setTextColor(Color.rgb(160, 182, 193));
        subtitle.setPadding(0, 0, 0, dp12);
        root.addView(subtitle);

        // --- FULLSCREEN TOUCH POSITION EDITOR BUTTON ---
        Button fullscreenBtn = new Button(context);
        fullscreenBtn.setText("🖥️   Open Fullscreen Touch Position Editor");
        fullscreenBtn.setTextColor(Color.rgb(22, 31, 32));
        fullscreenBtn.setBackgroundColor(Color.rgb(224, 191, 115));
        fullscreenBtn.setOnClickListener(v -> showFullscreenMapper(context, currentLayout, p));
        LinearLayout.LayoutParams fsLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        fsLp.setMargins(0, dp8, 0, dp12);
        root.addView(fullscreenBtn, fsLp);

        // --- SCALE & OPACITY CONTROLS ---
        LinearLayout optRow = new LinearLayout(context);
        optRow.setOrientation(LinearLayout.HORIZONTAL);
        optRow.setPadding(0, dp8, 0, dp8);

        LinearLayout scaleCol = new LinearLayout(context);
        scaleCol.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams lpCol = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        scaleCol.setLayoutParams(lpCol);

        TextView scaleLabel = new TextView(context);
        scaleLabel.setText("GUI SCALE:");
        scaleLabel.setTextSize(12);
        scaleLabel.setTextColor(Color.rgb(135, 163, 181));
        scaleCol.addView(scaleLabel);

        Spinner scaleSpinner = new Spinner(context);
        String[] scaleOptions = {"80% (Compact)", "100% (Standard)", "120% (Large)", "140% (Extra Large)"};
        Integer[] scaleValues = {80, 100, 120, 140};
        ArrayAdapter<String> scaleAdapter = new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, scaleOptions);
        scaleSpinner.setAdapter(scaleAdapter);
        int scaleSel = 1;
        for (int i = 0; i < scaleValues.length; i++) {
            if (scaleValues[i] == savedScale) scaleSel = i;
        }
        scaleSpinner.setSelection(scaleSel);
        scaleCol.addView(scaleSpinner);
        optRow.addView(scaleCol);

        LinearLayout opCol = new LinearLayout(context);
        opCol.setOrientation(LinearLayout.VERTICAL);
        opCol.setLayoutParams(lpCol);

        TextView opLabel = new TextView(context);
        opLabel.setText("TRANSPARENCY:");
        opLabel.setTextSize(12);
        opLabel.setTextColor(Color.rgb(135, 163, 181));
        opCol.addView(opLabel);

        Spinner opSpinner = new Spinner(context);
        String[] opOptions = {"100% (Solid)", "85% (Mild)", "70% (Translucent)", "50% (Minimal)"};
        Integer[] opValues = {100, 85, 70, 50};
        ArrayAdapter<String> opAdapter = new ArrayAdapter<>(context, android.R.layout.simple_spinner_dropdown_item, opOptions);
        opSpinner.setAdapter(opAdapter);
        int opSel = 0;
        for (int i = 0; i < opValues.length; i++) {
            if (opValues[i] == savedOpacity) opSel = i;
        }
        opSpinner.setSelection(opSel);
        opCol.addView(opSpinner);
        optRow.addView(opCol);

        root.addView(optRow);

        // --- START COLLAPSED ---
        CheckBox collapsedCheck = new CheckBox(context);
        collapsedCheck.setText("Start panel collapsed (tap arrow to expand)");
        collapsedCheck.setChecked(savedCollapsed);
        collapsedCheck.setTextColor(Color.rgb(220, 230, 237));
        root.addView(collapsedCheck);

        // --- BUTTON MAPPING LIST ---
        TextView mapHeader = new TextView(context);
        mapHeader.setText("ACTIVE TOUCH BUTTONS & ORDER:");
        mapHeader.setTextSize(12);
        mapHeader.setTextColor(Color.rgb(135, 163, 181));
        mapHeader.setPadding(0, dp12, 0, dp4(density));
        root.addView(mapHeader);

        LinearLayout listContainer = new LinearLayout(context);
        listContainer.setOrientation(LinearLayout.VERTICAL);
        root.addView(listContainer);

        Runnable updateList = new Runnable() {
            @Override
            public void run() {
                listContainer.removeAllViews();
                for (int i = 0; i < currentLayout.size(); i++) {
                    final int index = i;
                    final ActionItem item = currentLayout.get(i);

                    LinearLayout row = new LinearLayout(context);
                    row.setOrientation(LinearLayout.HORIZONTAL);
                    row.setGravity(Gravity.CENTER_VERTICAL);
                    row.setPadding(dp8, dp4(density), dp8, dp4(density));

                    CheckBox cb = new CheckBox(context);
                    cb.setChecked(item.active);
                    cb.setText(String.format(Locale.US, "%s [%s]", item.label, item.defaultHotkey));
                    cb.setTextColor(item.active ? Color.rgb(240, 244, 248) : Color.rgb(120, 130, 140));
                    cb.setOnCheckedChangeListener((buttonView, isChecked) -> item.active = isChecked);

                    LinearLayout.LayoutParams cbLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
                    row.addView(cb, cbLp);

                    Button upBtn = new Button(context);
                    upBtn.setText("▲");
                    upBtn.setTextSize(12);
                    upBtn.setPadding(0, 0, 0, 0);
                    upBtn.setEnabled(index > 0);
                    upBtn.setOnClickListener(v -> {
                        ActionItem prev = currentLayout.get(index - 1);
                        currentLayout.set(index - 1, item);
                        currentLayout.set(index, prev);
                        run();
                    });
                    LinearLayout.LayoutParams btnLp = new LinearLayout.LayoutParams(dp36(density), dp36(density));
                    row.addView(upBtn, btnLp);

                    Button downBtn = new Button(context);
                    downBtn.setText("▼");
                    downBtn.setTextSize(12);
                    downBtn.setPadding(0, 0, 0, 0);
                    downBtn.setEnabled(index < currentLayout.size() - 1);
                    downBtn.setOnClickListener(v -> {
                        ActionItem next = currentLayout.get(index + 1);
                        currentLayout.set(index + 1, item);
                        currentLayout.set(index, next);
                        run();
                    });
                    row.addView(downBtn, btnLp);

                    listContainer.addView(row);
                }
            }
        };

        updateList.run();

        ScrollView scroll = new ScrollView(context);
        scroll.addView(root);

        new AlertDialog.Builder(context)
                .setTitle("🎮   Touch Control Settings")
                .setView(scroll)
                .setPositiveButton("Save", (dialog, which) -> {
                    savePreferences(p, currentLayout, scaleValues[scaleSpinner.getSelectedItemPosition()],
                            opValues[opSpinner.getSelectedItemPosition()], collapsedCheck.isChecked());
                })
                .setNeutralButton("Hotkey Guide", (dialog, which) -> guide(context))
                .setNegativeButton("Cancel", null)
                .show();
    }

    private static void savePreferences(SharedPreferences p, List<ActionItem> layout, int scale, int opacity, boolean collapsed) {
        int maskVal = 0;
        StringBuilder mappingSb = new StringBuilder();
        StringBuilder posSb = new StringBuilder();

        for (ActionItem item : layout) {
            if (item.active) {
                if (item.maskBit >= 0) maskVal |= (1 << item.maskBit);
                if (mappingSb.length() > 0) mappingSb.append(",");
                mappingSb.append(item.id);

                if (item.xPct >= 0 && item.yPct >= 0) {
                    if (posSb.length() > 0) posSb.append(";");
                    posSb.append(String.format(Locale.US, "%s:%.1f,%.1f", item.id, item.xPct, item.yPct));
                }
            }
        }

        p.edit()
                .putInt("touch_actions", maskVal)
                .putString("touch_mapping", mappingSb.toString())
                .putString("touch_positions", posSb.toString())
                .putInt("touch_scale", scale)
                .putInt("touch_opacity", opacity)
                .putBoolean("touch_collapsed", collapsed)
                .apply();
    }

    private static void showFullscreenMapper(Context context, List<ActionItem> layout, SharedPreferences prefs) {
        final Dialog dialog = new Dialog(context, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
        Window window = dialog.getWindow();
        if (window != null) {
            window.setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);
        }

        float density = context.getResources().getDisplayMetrics().density;
        FrameLayout root = new FrameLayout(context);
        root.setBackgroundColor(Color.rgb(14, 20, 22));

        TextView topHeader = new TextView(context);
        topHeader.setText("🖥️   FULLSCREEN TOUCH POSITION EDITOR — Drag buttons anywhere on screen");
        topHeader.setTextSize(13);
        topHeader.setTextColor(Color.rgb(224, 191, 115));
        topHeader.setPadding(dp16(density), dp12(density), dp16(density), dp12(density));
        topHeader.setBackgroundColor(Color.argb(200, 20, 30, 35));

        FrameLayout.LayoutParams headerLp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP);
        root.addView(topHeader, headerLp);

        final FullscreenDragCanvas canvasView = new FullscreenDragCanvas(context, layout);
        root.addView(canvasView, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout btmBar = new LinearLayout(context);
        btmBar.setOrientation(LinearLayout.HORIZONTAL);
        btmBar.setPadding(dp16(density), dp8(density), dp16(density), dp16(density));
        btmBar.setBackgroundColor(Color.argb(220, 18, 24, 28));

        Button resetBtn = new Button(context);
        resetBtn.setText("Reset Dock Positions");
        resetBtn.setOnClickListener(v -> {
            for (ActionItem item : layout) { item.xPct = -1f; item.yPct = -1f; }
            canvasView.invalidate();
        });

        Button closeBtn = new Button(context);
        closeBtn.setText("Save & Close");
        closeBtn.setOnClickListener(v -> {
            savePreferences(prefs, layout, prefs.getInt("touch_scale", 100), prefs.getInt("touch_opacity", 100),
                    prefs.getBoolean("touch_collapsed", true));
            dialog.dismiss();
        });

        LinearLayout.LayoutParams btnLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        btnLp.setMargins(4, 0, 4, 0);
        btmBar.addView(resetBtn, btnLp);
        btmBar.addView(closeBtn, btnLp);

        FrameLayout.LayoutParams btmLp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM);
        root.addView(btmBar, btmLp);

        dialog.setContentView(root);
        dialog.show();
    }

    private static int dp4(float density) { return Math.round(4 * density); }
    private static int dp8(float density) { return Math.round(8 * density); }
    private static int dp12(float density) { return Math.round(12 * density); }
    private static int dp16(float density) { return Math.round(16 * density); }
    private static int dp36(float density) { return Math.round(36 * density); }

    private static List<ActionItem> parseLayout(String savedMapping, String savedPositions, int mask) {
        List<ActionItem> list = new ArrayList<>();
        Map<String, float[]> posMap = new HashMap<>();

        if (savedPositions != null && !savedPositions.isEmpty()) {
            for (String p : savedPositions.split(";")) {
                String[] parts = p.split(":");
                if (parts.length == 2) {
                    String[] coords = parts[1].split(",");
                    if (coords.length == 2) {
                        try {
                            float x = Float.parseFloat(coords[0]);
                            float y = Float.parseFloat(coords[1]);
                            posMap.put(parts[0], new float[]{x, y});
                        } catch (Exception ignored) { }
                    }
                }
            }
        }

        if (savedMapping != null && !savedMapping.isEmpty()) {
            String[] tokens = savedMapping.split(",");
            for (String t : tokens) {
                for (ActionItem a : ALL_ACTIONS) {
                    if (a.id.equals(t)) {
                        ActionItem item = new ActionItem(a.id, a.label, a.defaultHotkey, a.maskBit, true);
                        float[] coords = posMap.get(a.id);
                        if (coords != null) {
                            item.xPct = coords[0];
                            item.yPct = coords[1];
                        }
                        list.add(item);
                        break;
                    }
                }
            }
            for (ActionItem a : ALL_ACTIONS) {
                boolean found = false;
                for (ActionItem existing : list) {
                    if (existing.id.equals(a.id)) { found = true; break; }
                }
                if (!found) {
                    ActionItem item = new ActionItem(a.id, a.label, a.defaultHotkey, a.maskBit, false);
                    float[] coords = posMap.get(a.id);
                    if (coords != null) {
                        item.xPct = coords[0];
                        item.yPct = coords[1];
                    }
                    list.add(item);
                }
            }
        } else {
            for (ActionItem a : ALL_ACTIONS) {
                boolean active = a.maskBit >= 0 && (mask & (1 << a.maskBit)) != 0;
                ActionItem item = new ActionItem(a.id, a.label, a.defaultHotkey, a.maskBit, active);
                float[] coords = posMap.get(a.id);
                if (coords != null) {
                    item.xPct = coords[0];
                    item.yPct = coords[1];
                }
                list.add(item);
            }
        }
        return list;
    }

    /** Fullscreen canvas supporting free drag and drop placement of virtual buttons anywhere. */
    private static class FullscreenDragCanvas extends View {
        private final List<ActionItem> layout;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private int draggedIndex = -1;
        private final List<RectF> btnRects = new ArrayList<>();
        private final List<ActionItem> activeBtns = new ArrayList<>();

        FullscreenDragCanvas(Context context, List<ActionItem> layout) {
            super(context);
            this.layout = layout;
        }

        @Override
        public boolean performClick() { return super.performClick(); }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            float x = event.getX();
            float y = event.getY();
            int w = getWidth();
            int h = getHeight();

            switch (event.getAction()) {
                case MotionEvent.ACTION_DOWN:
                    draggedIndex = -1;
                    for (int i = 0; i < btnRects.size(); i++) {
                        if (btnRects.get(i).contains(x, y)) {
                            draggedIndex = i;
                            break;
                        }
                    }
                    break;
                case MotionEvent.ACTION_MOVE:
                    if (draggedIndex >= 0 && draggedIndex < activeBtns.size() && w > 0 && h > 0) {
                        ActionItem item = activeBtns.get(draggedIndex);
                        item.xPct = Math.max(0f, Math.min(100f, (x / (float) w) * 100f));
                        item.yPct = Math.max(10f, Math.min(90f, (y / (float) h) * 100f));
                        invalidate();
                    }
                    break;
                case MotionEvent.ACTION_UP:
                    if (draggedIndex >= 0) {
                        performClick();
                        draggedIndex = -1;
                        invalidate();
                    }
                    break;
            }
            return true;
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            int w = getWidth();
            int h = getHeight();

            btnRects.clear();
            activeBtns.clear();

            // Background battlefield preview
            paint.setColor(Color.rgb(18, 28, 26));
            canvas.drawRect(0, 0, w, h, paint);

            paint.setColor(Color.rgb(28, 44, 40));
            paint.setStrokeWidth(2f);
            for (int gx = 0; gx < w; gx += 60) canvas.drawLine(gx, 0, gx, h, paint);
            for (int gy = 0; gy < h; gy += 60) canvas.drawLine(0, gy, w, gy, paint);

            float btnW = 90;
            float btnH = 55;

            for (ActionItem item : layout) {
                if (!item.active) continue;
                activeBtns.add(item);

                float bx, by;
                if (item.xPct >= 0 && item.yPct >= 0) {
                    bx = (item.xPct / 100f) * (w - btnW);
                    by = (item.yPct / 100f) * (h - btnH);
                } else {
                    int idx = activeBtns.size() - 1;
                    int cols = 4;
                    bx = 20 + (idx % cols) * (btnW + 12);
                    by = h - 190 + ((float)idx / cols) * (btnH + 12);
                }

                RectF r = new RectF(bx, by, bx + btnW, by + btnH);
                btnRects.add(r);

                paint.setColor(Color.rgb(45, 68, 62));
                canvas.drawRoundRect(r, 6, 6, paint);

                paint.setColor(Color.rgb(80, 190, 160));
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(2.5f);
                canvas.drawRoundRect(r, 6, 6, paint);
                paint.setStyle(Paint.Style.FILL);

                paint.setColor(Color.rgb(240, 248, 245));
                paint.setTextSize(14f);
                paint.setTextAlign(Paint.Align.CENTER);
                canvas.drawText(item.id, bx + btnW / 2, by + btnH / 2 + 5, paint);
            }
        }
    }

    private static void guide(Context context) {
        new AlertDialog.Builder(context).setTitle("Commands & Gestures Guide")
                .setMessage("Changes apply on your next game launch. MORE and control groups remain available.\n\n" +
                        "MORE → ROUTE Z: enter waypoint mode, tap route points, then tap ROUTE Z again to start. X on the mode banner cancels the uncommitted route. Only movement routes are supported.\n\n" +
                        "Stop (S) clears a committed route. A new normal order replaces it.\n\n" +
                        "D: Deploy, pack or unload the selected unit.\nG: Guard. X: Scatter.\n" +
                        "Ctrl: Force Fire. Ctrl + Shift: Attack Move.\n" +
                        "Double tap: select matching units on screen.\nPinch: zoom. Two-finger tap: cancel.\n" +
                        "Tap group: select. Hold group: assign.\n\n" +
                        "Keyboard bindings can be changed in the game's Keyboard options; the labels above show defaults.")
                .setPositiveButton("OK", null).show();
    }
}

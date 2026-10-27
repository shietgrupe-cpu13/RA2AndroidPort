package org.ra2.engine;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.FrameLayout;
import android.widget.ProgressBar;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * W2-AR-03 / W2-AR-04: the launcher activity. It checks that the user's own
 * Red Alert 2 files are in the app's external files dir
 * (Android/data/org.ra2.engine/files/ra2/, the folder the engine reads) and
 * starts the game when they are. Otherwise it explains what is missing and
 * offers an in-app import: the Storage Access Framework folder picker
 * (ACTION_OPEN_DOCUMENT_TREE -- works on Android 5 to 15, no storage
 * permission) copies the files in under their canonical names. That replaces
 * the old "adb push into Android/data" recipe, which Android 11+ refuses for
 * the shell user and file managers.
 *
 * Runs in its own process (":setup", see the manifest) so the engine's
 * exit() at the end of main() cannot take it down when the engine hands back
 * to it (the missing-files page's IMPORT button).
 */
public class SetupActivity extends Activity {
    private static final String TAG = "RA2Setup";
    private static final int REQ_TREE = 1;
    private static final int REQ_MAPS_TREE = 2;
    private static final int REQ_MOD_TREE = 5;
    private static final int REQ_CAMPAIGN_IMPORT = 3;
    private static final int REQ_CAMPAIGN_EXPORT = 4;
    public static final String EXTRA_IMPORT = "org.ra2.engine.IMPORT";
    /** W6-06-05: the launcher shortcut ("Import game files", res/xml/shortcuts.xml). */
    public static final String ACTION_IMPORT = "org.ra2.engine.action.IMPORT";

    /** The files the engine reads; the first four are required (engine/data_dir.inc). */
    static final String[] FILES = {
        "ra2.mix", "ra2md.mix", "language.mix", "multimd.mix",
        "langmd.mix", "mapsmd03.mix", "theme.mix", "expand01.mix",
        "Maps01.mix", "MAPS02.MIX", "ra2.csf",
    };
    static final int REQUIRED = 4;

    /**
     * W6-06-06: the file a picked document is copied to (relative to the data
     * dir), or null when the engine does not read it. The fixed FILES keep
     * their retail spelling; the patch archives the desktop AssetFS scans
     * (src/asset_fs.cpp: expandNN / ecacheNN / elocalNN), thememd.mix and the
     * movies (MOVIESnn.MIX / MOVMDnn.MIX archives and loose .bik files, which
     * engine/movie_player.inc finds in the data dir and its Movies/ folder,
     * case-insensitively) are copied too.
     */
    static String target(String name) {
        if (name == null) return null;
        if(name.isEmpty() || name.startsWith(".") || name.length()>200 ||
            name.indexOf('/')>=0 || name.indexOf('\\')>=0 || name.indexOf(':')>=0)return null;
        for(int i=0;i<name.length();++i)if(name.charAt(i)<32 || name.charAt(i)==127)return null;
        String l = name.toLowerCase(Locale.ROOT);
        if(l.endsWith(".yrm") || l.endsWith(".map") || l.endsWith(".mpr"))return "maps/"+l;
        for (String f : FILES) if (f.toLowerCase(Locale.ROOT).equals(l)) return f;
        if (l.matches("(expand|ecache|elocal)\\d\\d\\.mix")) return l;
        if (l.equals("thememd.mix")) return l;
        if (l.matches("(movies\\d\\d|movmd\\d\\d)\\.mix")) return l;
        if (l.endsWith(".bik") && l.length() > 4) return "Movies/" + l;
        return null;
    }

    private TextView status, title, readiness;
    private ProgressBar progress;
    private Button importButton, mapsButton, campaignImportButton, campaignExportButton;
    private Button startButton, exitButton, detailsButton;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean copying = false;
    private static final String[] THEMES={"Command Center","Allied Blue","Soviet Red","Yuri Violet","Emerald","Desert Gold","Arctic","Midnight OLED"};
    // Background, surface, accent. All text retains a high-contrast light color.
    private static final int[][] PALETTES={
        {0xff0a1219,0xff1c2a34,0xff70cdc1},{0xff081526,0xff152c45,0xff83c9ff},
        {0xff200b10,0xff3b1a22,0xffffaaaa},{0xff180e26,0xff30203f,0xffd5adff},
        {0xff071b17,0xff14382e,0xff8de4b4},{0xff20190c,0xff37301d,0xfff0d291},
        {0xff101d29,0xff263c4a,0xffb8e9fa},{0xff000000,0xff111316,0xffc7cedb}};
    private int[] theme(){int n=getPreferences(MODE_PRIVATE).getInt("launcher_theme",0);return PALETTES[Math.max(0,Math.min(PALETTES.length-1,n))];}
    private void chooseTheme(){
        if(copying)return;
        new AlertDialog.Builder(this).setTitle("Launcher appearance")
            .setSingleChoiceItems(THEMES,getPreferences(MODE_PRIVATE).getInt("launcher_theme",0),(dialog,which)->{
                getPreferences(MODE_PRIVATE).edit().putInt("launcher_theme",which).apply();
                dialog.dismiss();buildUi();refresh();
            }).setNegativeButton("Close",null).show();
    }
    // R-161: the copy thread is owned by the activity; leaving (finish /
    // destroy) cancels it, and its partial file is deleted.
    private Thread importThread;
    private volatile boolean cancelImport = false;

    static File dataDir(Activity a) {
        File ext = a.getExternalFilesDir(null);
        File d = new File(ext != null ? ext : a.getFilesDir(), "ra2");
        //noinspection ResultOfMethodCallIgnored
        d.mkdirs();   // visible over MTP even before the first import
        return d;
    }

    static List<String> missing(File dir, boolean requiredOnly) {
        List<String> m = new ArrayList<>();
        for (int i = 0; i < (requiredOnly ? REQUIRED : FILES.length); ++i) {
            File f = new File(dir, FILES[i]);
            if (!f.isFile() || f.length() == 0) m.add(FILES[i]);
        }
        return m;
    }

    /** R-161: delete *.part leftovers of an interrupted copy (app killed mid-import). */
    static int cleanParts(File dir) {
        int n = 0;
        File[] list = dir.listFiles();
        if (list == null) return 0;
        for (File f : list) {
            if (f.isFile() && f.getName().endsWith(".part") && f.delete()) ++n;
            else if (f.isDirectory() && f.getName().equals("Movies")) n += cleanParts(f);
        }
        if (n > 0) Log.i(TAG, "removed " + n + " partial file(s) of an interrupted import");
        return n;
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        File dir = dataDir(this);
        cleanParts(dir);
        // Keep the launcher reachable after a crash so reports can be shared.
        buildUi();
        refresh();
        if (!LegalNotice.accepted(this)) LegalNotice.show(this,this::refresh);
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private Button button(String label, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setMinHeight(dp(56));   // >= 48 dp touch target
        b.setTextColor(Color.rgb(225, 234, 240));
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        b.setAllCaps(false);
        b.setBackground(panel(theme()[1],theme()[2]));
        b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, dp(6), 0, dp(6));
        b.setPadding(dp(18),dp(12),dp(18),dp(12));
        b.setLayoutParams(lp);
        return b;
    }

    private GradientDrawable panel(int fill, int edge) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(12));
        d.setStroke(dp(1), edge);
        return d;
    }

    private TextView label(String text, int size, int color) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
        t.setTextColor(color);
        t.setPadding(dp(8), dp(6), dp(8), dp(6));
        return t;
    }

    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(18), dp(20), dp(18), dp(20));
        ScrollView page = new ScrollView(this);
        page.setFillViewport(true);
        page.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{theme()[0],theme()[1],theme()[0]}));
        FrameLayout centered=new FrameLayout(this);
        centered.addView(root,new FrameLayout.LayoutParams(
                Math.min(getResources().getDisplayMetrics().widthPixels,dp(600)),
                ViewGroup.LayoutParams.WRAP_CONTENT,Gravity.TOP|Gravity.CENTER_HORIZONTAL));
        page.addView(centered);

        RelativeLayout headerBar = new RelativeLayout(this);
        LinearLayout headerText = new LinearLayout(this);
        headerText.setOrientation(LinearLayout.VERTICAL);

        TextView eyebrow=label("BRYLE13  /  ANDROID EDITION",11,theme()[2]);
        eyebrow.setLetterSpacing(0.12f);
        headerText.addView(eyebrow);
        title = label("RA2 PORT", 32, Color.rgb(244,240,223));
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        headerText.addView(title);

        RelativeLayout.LayoutParams textLp = new RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        textLp.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
        headerBar.addView(headerText, textLp);

        Button settingsBtn = new Button(this);
        settingsBtn.setText("⚙");
        settingsBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        settingsBtn.setTextColor(Color.rgb(244, 240, 223));
        settingsBtn.setPadding(0, 0, 0, 0);
        settingsBtn.setBackground(panel(Color.rgb(32, 48, 54), Color.rgb(80, 120, 110)));
        settingsBtn.setOnClickListener(v -> showSettingsMenu());

        RelativeLayout.LayoutParams btnLp = new RelativeLayout.LayoutParams(dp(44), dp(44));
        btnLp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        btnLp.addRule(RelativeLayout.CENTER_VERTICAL);
        headerBar.addView(settingsBtn, btnLp);

        LinearLayout.LayoutParams headerLp = new LinearLayout.LayoutParams(-1, -2);
        headerLp.setMargins(0, 0, 0, dp(12));
        root.addView(headerBar, headerLp);

        readiness = label("Checking game files...", 14, Color.rgb(140, 225, 185));
        readiness.setBackground(panel(Color.rgb(20, 39, 43), Color.rgb(45, 92, 86)));
        readiness.setPadding(dp(14), dp(10), dp(14), dp(10));
        LinearLayout.LayoutParams readLp = new LinearLayout.LayoutParams(-1, -2);
        readLp.setMargins(0, 0, 0, dp(12));
        root.addView(readiness, readLp);

        startButton = button("▶   PLAY YURI'S REVENGE", v -> startGame());
        startButton.setMinHeight(dp(64));
        startButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        startButton.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        startButton.setTextColor(Color.rgb(22,31,32));
        startButton.setBackgroundTintList(null);
        startButton.setBackground(panel(Color.rgb(224,191,115), Color.rgb(247,220,156)));
        LinearLayout.LayoutParams playLp = new LinearLayout.LayoutParams(-1, -2);
        playLp.setMargins(0, 0, 0, dp(16));
        root.addView(startButton, playLp);

        root.addView(label("GAME ASSETS & MAPS", 12, Color.rgb(135, 163, 181)));
        importButton = button("📁   Import or update game files", v -> pickFolder());
        root.addView(importButton);
        mapsButton = button("🗺   Import custom maps", v -> pickMapsFolder());
        root.addView(mapsButton);
        root.addView(button("Mods & compatibility", v -> { if(!copying) ModManager.show(this,REQ_MOD_TREE); }));

        root.addView(label("CAMPAIGN PROGRESS",12,Color.rgb(135,163,181)));
        campaignImportButton = button("🏆   Import campaign progress", v -> pickCampaignProgress(false));
        root.addView(campaignImportButton);
        campaignExportButton = button("⭐   Export campaign progress", v -> pickCampaignProgress(true));
        root.addView(campaignExportButton);

        status = label("", 13, Color.rgb(220, 230, 237));
        status.setPadding(dp(14), dp(10), dp(14), dp(10));
        status.setBackground(panel(Color.rgb(20, 31, 41), Color.rgb(44, 61, 73)));
        status.setVisibility(View.GONE);
        detailsButton=button("🔍   Game files & setup details  +",v->{
            boolean open=status.getVisibility()!=View.VISIBLE;
            status.setVisibility(open?View.VISIBLE:View.GONE);
            ((Button)v).setText(open?"🔍   Hide setup details  −":"🔍   Game files & setup details  +");
        });
        root.addView(detailsButton);root.addView(status);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        root.addView(progress);

        exitButton = button("🚪   " + getString(R.string.setup_exit), v -> finish());
        LinearLayout.LayoutParams exitLp = new LinearLayout.LayoutParams(-1, -2);
        exitLp.setMargins(0, dp(12), 0, dp(8));
        root.addView(exitButton, exitLp);

        TextView footer = label("Android port by Bryle13. Original engine by Omar El Arab.", 11, Color.rgb(120, 145, 160));
        footer.setGravity(Gravity.CENTER);
        root.addView(footer);

        setContentView(page);
    }

    private void refresh() {
        File dir = dataDir(this);
        List<String> req = missing(dir, true);
        List<String> all = missing(dir, false);
        StringBuilder sb = new StringBuilder();
        sb.append(getString(R.string.setup_intro)).append("\n\n");
        sb.append("After a crash, tap Share game report before starting another game.\n\n");
        sb.append(getString(R.string.setup_folder)).append("\n  ").append(dir.getAbsolutePath()).append("\n\n");
        // W6-06-05: opened to add / replace files with everything present.
        title.setText("RA2 PORT");
        readiness.setText(copying ? "IMPORTING GAME FILES..." :
                (req.isEmpty() ? "READY TO PLAY - Yuri's Revenge" :
                 "SETUP REQUIRED - " + req.size() + " required files missing"));
        if (req.isEmpty()) {
            sb.append(getString(R.string.setup_ready)).append("\n");
        } else {
            sb.append(getString(R.string.setup_missing_required)).append("\n");
            for (String f : req) sb.append("  ").append(f).append("\n");
        }
        List<String> opt = new ArrayList<>(all);
        opt.removeAll(req);
        if (!opt.isEmpty()) {
            sb.append("\n").append(getString(R.string.setup_missing_optional)).append("\n");
            for (String f : opt) sb.append("  ").append(f).append("\n");
        }
        sb.append("\n").append(getString(R.string.setup_how));
        sb.append("\n\n").append(getString(R.string.setup_reimport_note));   // W6-06-05
        status.setText(sb.toString());
        if (!req.isEmpty() || copying) status.setVisibility(View.VISIBLE);
        detailsButton.setText(status.getVisibility()==View.VISIBLE?"Hide setup details  −":"Game files & setup details  +");
        startButton.setEnabled(req.isEmpty() && !copying);
        startButton.setAlpha(req.isEmpty() && !copying ? 1f : 0.45f);
        importButton.setEnabled(!copying);
        mapsButton.setEnabled(!copying);
        campaignImportButton.setEnabled(!copying);
        campaignExportButton.setEnabled(!copying && campaignProgressFile().isFile());
        exitButton.setEnabled(!copying);   // R-161: no Exit in the middle of a copy
    }

    /** R-161: Back does not abandon a running copy either. */
    @Override
    public void onBackPressed() {
        if (copying) return;
        super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        if (importThread != null && importThread.isAlive()) {
            cancelImport = true;   // the thread deletes its .part file and stops
            try { importThread.join(2000); } catch (InterruptedException ignored) { }
        }
        super.onDestroy();
    }

    private void pickFolder() {
        pickFolder(REQ_TREE);
    }

    private void pickMapsFolder() {
        pickFolder(REQ_MAPS_TREE);
    }

    private void pickFolder(int requestCode) {
        if (!LegalNotice.accepted(this)) {
            LegalNotice.show(this, () -> pickFolder(requestCode));
            return;
        }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        // R-160: open the picker at Download/RA2 (where the instructions say
        // to put the files). Android 11+ refuses Download itself as a tree,
        // so the user must open the RA2 sub-folder and tap USE THIS FOLDER.
        if (Build.VERSION.SDK_INT >= 26) {
            i.putExtra(DocumentsContract.EXTRA_INITIAL_URI, DocumentsContract.buildDocumentUri(
                    "com.android.externalstorage.documents", "primary:Download/RA2"));
        }
        startActivityForResult(i, requestCode);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req==REQ_MOD_TREE) {
            if(res==RESULT_OK && data!=null && data.getData()!=null)ModManager.importFolder(this,data.getData());
            return;
        }
        if (req == REQ_CAMPAIGN_IMPORT) {
            if (res == RESULT_OK && data != null && data.getData() != null)
                importCampaignProgress(data.getData());
            return;
        }
        if (req == REQ_CAMPAIGN_EXPORT) {
            if (res == RESULT_OK && data != null && data.getData() != null)
                exportCampaignProgress(data.getData());
            return;
        }
        if ((req != REQ_TREE && req != REQ_MAPS_TREE) || res != RESULT_OK ||
                data == null || data.getData() == null) return;
        final Uri tree = data.getData();
        final boolean mapsOnly = req == REQ_MAPS_TREE;
        copying = true;
        cancelImport = false;
        progress.setVisibility(View.VISIBLE);
        progress.setProgress(0);
        refresh();
        importThread = new Thread(() -> importFrom(tree, mapsOnly), "ra2-import");
        importThread.start();
    }

    private File campaignProgressFile() {
        return new File(getFilesDir(), "Saved Games/CAMPAIGN.PROGRESS");
    }

    private void pickCampaignProgress(boolean export) {
        Intent i = new Intent(export ? Intent.ACTION_CREATE_DOCUMENT : Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("application/octet-stream");
        if (export) {
            i.putExtra(Intent.EXTRA_TITLE, "RA2-CAMPAIGN.PROGRESS");
            startActivityForResult(i, REQ_CAMPAIGN_EXPORT);
        } else {
            startActivityForResult(i, REQ_CAMPAIGN_IMPORT);
        }
    }

    private void importCampaignProgress(Uri uri) {
        File target = campaignProgressFile();
        File parent = target.getParentFile();
        File temp = new File(parent, target.getName() + ".part");
        try {
            if (parent == null || (!parent.isDirectory() && !parent.mkdirs()))
                throw new IOException("Cannot create campaign save folder");
            byte[] data;
            try (InputStream in = getContentResolver().openInputStream(uri);
                 ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                if (in == null) throw new IOException("Cannot open selected file");
                byte[] buffer = new byte[1024];
                int n;
                while ((n = in.read(buffer)) != -1) {
                    if (bytes.size() + n > 8192) throw new IOException("Campaign file is too large");
                    bytes.write(buffer, 0, n);
                }
                data = bytes.toByteArray();
            }
            String text = new String(data, StandardCharsets.US_ASCII);
            if (!text.matches("RA2-CAMPAIGN-PROGRESS 1\\r?\\nMap=(?!.*\\.\\.)[A-Za-z0-9_.-]{1,32}\\r?\\n" +
                    "Side=(GDI|NOD)\\r?\\nDifficulty=[0-2]\\r?\\n"))
                throw new IOException("Invalid campaign progress file");
            try (FileOutputStream out = new FileOutputStream(temp)) {
                out.write(data);
                out.getFD().sync();
            }
            if (target.exists() && !target.delete()) throw new IOException("Cannot replace campaign progress");
            if (!temp.renameTo(target)) throw new IOException("Cannot save campaign progress");
            Toast.makeText(this, "Campaign progress imported. It will be available when you start the game.",
                    Toast.LENGTH_LONG).show();
        } catch (Exception ex) {
            //noinspection ResultOfMethodCallIgnored
            temp.delete();
            Log.e(TAG, "campaign progress import failed", ex);
            Toast.makeText(this, "Campaign progress import failed: " + ex.getMessage(), Toast.LENGTH_LONG).show();
        }
        refresh();
    }

    private void exportCampaignProgress(Uri uri) {
        File source = campaignProgressFile();
        try {
            if (!source.isFile() || source.length() > 8192)
                throw new IOException("No valid campaign progress to export");
            try (InputStream in = new FileInputStream(source);
                 OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
                if (out == null) throw new IOException("Cannot open destination");
                byte[] buffer = new byte[1024];
                int n;
                while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
                out.flush();
            }
            Toast.makeText(this, "Campaign progress exported.", Toast.LENGTH_LONG).show();
        } catch (Exception ex) {
            Log.e(TAG, "campaign progress export failed", ex);
            Toast.makeText(this, "Campaign progress export failed: " + ex.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    private void showSettingsMenu() {
        final Dialog dialog = new Dialog(this);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(24), dp(20), dp(20));
        root.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{theme()[0], theme()[1], theme()[0]}));

        TextView titleView = new TextView(this);
        titleView.setText("⚙   SETTINGS DASHBOARD");
        titleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        titleView.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        titleView.setTextColor(theme()[2]);
        titleView.setPadding(0, 0, 0, dp(16));
        root.addView(titleView);

        String[] categories = {
            "🎮   Control Settings (Touch Mapping & Free Placement)",
            "🖥️   Graphics & Display Settings",
            "⚡   Performance & FPS Settings",
            "🕹️   Game Options & Main Menu Mode",
            "🎨   Launcher Theme & Appearance",
            "📊   Diagnostics & Game Report",
            "📜   Terms, Credits & Licenses"
        };

        ScrollView scroll = new ScrollView(this);
        LinearLayout listLayout = new LinearLayout(this);
        listLayout.setOrientation(LinearLayout.VERTICAL);

        for (int i = 0; i < categories.length; i++) {
            final int idx = i;
            Button catBtn = button(categories[i], v -> {
                dialog.dismiss();
                switch (idx) {
                    case 0: if (!copying) TouchSettings.show(this); break;
                    case 1: showGraphicsSettings(); break;
                    case 2: showFpsSettings(); break;
                    case 3:
                        new AlertDialog.Builder(this).setTitle("Game main menu")
                            .setSingleChoiceItems(new String[]{"Original Yuri's Revenge","Command Center (experimental)"},
                                getSharedPreferences("interface", MODE_PRIVATE).getBoolean("command_menu", false) ? 1 : 0,
                                (d, w) -> { getSharedPreferences("interface", MODE_PRIVATE).edit()
                                    .putBoolean("command_menu", w == 1).apply(); d.dismiss(); })
                            .setNegativeButton("Close", null).show();
                        break;
                    case 4: chooseTheme(); break;
                    case 5: Diagnostics.share(this); break;
                    case 6: LegalNotice.show(this, this::refresh); break;
                }
            });
            listLayout.addView(catBtn);
        }
        scroll.addView(listLayout);
        root.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f));

        Button closeBtn = button("Close", v -> dialog.dismiss());
        closeBtn.setBackground(panel(theme()[1], theme()[2]));
        root.addView(closeBtn);

        dialog.setContentView(root);
        if (dialog.getWindow() != null) {
            dialog.getWindow().setLayout(Math.min(getResources().getDisplayMetrics().widthPixels - dp(40), dp(500)),
                    ViewGroup.LayoutParams.WRAP_CONTENT);
            dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        }
        dialog.show();
    }

    private void showGraphicsSettings() {
        SharedPreferences p = getSharedPreferences("interface", MODE_PRIVATE);
        boolean widescreen = p.getBoolean("widescreen_mode", true);

        CheckBox wsCheck = new CheckBox(this);
        wsCheck.setText("Widescreen / Fullscreen Fill (Expand viewport to device aspect ratio)");
        wsCheck.setChecked(widescreen);
        wsCheck.setTextColor(Color.rgb(240, 244, 248));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(16), dp(16), dp(16));
        content.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR, new int[]{theme()[0], theme()[1], theme()[0]}));
        content.addView(wsCheck);

        new AlertDialog.Builder(this)
            .setTitle("🖥️ Graphics & Display Settings")
            .setView(content)
            .setPositiveButton("Save", (dialog, which) -> {
                p.edit().putBoolean("widescreen_mode", wsCheck.isChecked()).apply();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void showFpsSettings() {
        SharedPreferences p = getSharedPreferences("interface", MODE_PRIVATE);
        int currentFps = p.getInt("fps_limit", 60);

        String[] options = {"30 FPS (Power Saver)", "45 FPS (Balanced)", "60 FPS (Smooth Standard)", "120 FPS / Unlimited (High Refresh)"};
        int[] values = {30, 45, 60, 120};
        int sel = 2;
        for (int i = 0; i < values.length; i++) {
            if (values[i] == currentFps) sel = i;
        }

        final int[] selected = {sel};
        new AlertDialog.Builder(this)
            .setTitle("⚡ Performance & FPS Settings")
            .setSingleChoiceItems(options, sel, (dialog, which) -> selected[0] = which)
            .setPositiveButton("Save", (dialog, which) -> {
                p.edit().putInt("fps_limit", values[selected[0]]).apply();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    /**
     * Lists the game files directly inside `docId` (case-insensitive, see
     * target()); map-only imports ignore non-map files; sub-folders go to
     * `subdirs` (document id, display name).
     */
    private void scan(ContentResolver cr, Uri tree, String docId,
                      Map<String, Uri> found, Map<String, Long> sizes, List<String[]> subdirs,
                      boolean mapsOnly) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        try (Cursor c = cr.query(children, new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_SIZE,
                DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            while (c != null && c.moveToNext()) {
                String name = c.getString(1);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(3))) {
                    if (subdirs != null) subdirs.add(new String[]{c.getString(0), name == null ? "" : name});
                    continue;
                }
                String key = mapsOnly
                        ? (isMapFile(name) ? target(name) : null)
                        : target(name);
                if (key == null || found.containsKey(key)) continue;
                found.put(key, DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0)));
                sizes.put(key, c.isNull(2) ? 0L : c.getLong(2));
            }
        }
    }

    private static boolean isMapFile(String name) {
        if (name == null) return false;
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(".yrm") || lower.endsWith(".map") || lower.endsWith(".mpr");
    }

    /** Copies recognized files, or just maps, found in the picked folder. */
    private void importFrom(Uri tree, boolean mapsOnly) {
        ContentResolver cr = getContentResolver();
        File dir = dataDir(this);
        Map<String, Uri> found = new HashMap<>();
        Map<String, Long> sizes = new HashMap<>();
        long total = 0;
        String error = null;
        try {
            // R-160: the picked folder itself, then (when it holds none of the
            // required files) one level of sub-folders -- picking the folder
            // that CONTAINS RA2 (e.g. the copied RA2-phone package) works too.
            List<String[]> subdirs = new ArrayList<>();
            scan(cr, tree, DocumentsContract.getTreeDocumentId(tree), found, sizes, subdirs, mapsOnly);
            boolean anyRequired = false;
            for (int i = 0; i < REQUIRED; ++i) anyRequired |= found.containsKey(FILES[i]);
            List<String[]> deeper = new ArrayList<>();
            for (String[] sub : subdirs) {
                // W6-06-06: a Movies folder next to the files is always read;
                // R-160: other sub-folders only when the picked folder holds
                // none of the required files (the folder that CONTAINS RA2).
                if (!anyRequired || sub[1].equalsIgnoreCase("movies") || sub[1].equalsIgnoreCase("maps"))
                    scan(cr, tree, sub[0], found, sizes, anyRequired ? null : deeper, mapsOnly);
            }
            for (String[] sub : deeper)
                if (sub[1].equalsIgnoreCase("movies") || sub[1].equalsIgnoreCase("maps"))
                    scan(cr, tree, sub[0], found, sizes, null, mapsOnly);
            if (found.isEmpty()) throw new IOException(getString(
                    mapsOnly ? R.string.setup_no_maps : R.string.setup_no_files));
            for (Long sz : sizes.values()) total += sz;
            long done = 0;
            byte[] buf = new byte[1 << 20];
            for (Map.Entry<String, Uri> e : found.entrySet()) {
                if (cancelImport) throw new IOException("cancelled");
                File out = new File(dir, e.getKey());
                File tmp = new File(dir, e.getKey() + ".part");
                File parent = out.getParentFile();
                //noinspection ResultOfMethodCallIgnored
                if (parent != null) parent.mkdirs();   // Movies/
                final String name = e.getKey();
                ui.post(() -> status.setText(getString(R.string.setup_copying, name)));
                try (InputStream in = cr.openInputStream(e.getValue());
                     OutputStream os = new FileOutputStream(tmp)) {
                    if (in == null) throw new IOException("cannot open " + name);
                    int n;long copied=0;
                    while ((n = in.read(buf)) > 0) {
                        copied+=n;
                        if(name.startsWith("maps/") && copied>16*1024*1024)
                            throw new IOException("Map exceeds 16 MiB: "+name);
                        if (cancelImport) throw new IOException("cancelled");
                        os.write(buf, 0, n);
                        done += n;
                        final int p = total > 0 ? (int) (done * 1000 / total) : 0;
                        ui.post(() -> progress.setProgress(p));
                    }
                }
                if(name.startsWith("maps/") && tmp.length()>16*1024*1024) {
                    tmp.delete();throw new IOException("Map exceeds 16 MiB: "+name);
                }
                Long want = sizes.get(name);
                if (want != null && want > 0 && tmp.length() != want)
                    throw new IOException(name + ": copied " + tmp.length() + " of " + want + " bytes");
                //noinspection ResultOfMethodCallIgnored
                out.delete();
                if (!tmp.renameTo(out)) throw new IOException("cannot rename " + tmp);
                Log.i(TAG, "imported " + name + " (" + out.length() + " bytes)");
            }
            Log.i(TAG, "import done: " + found.size() + " files, " + total + " bytes");
        } catch (Exception ex) {
            Log.e(TAG, "import failed", ex);
            error = ex.getMessage();
            cleanParts(dir);   // R-161: never leave a partial file behind
        }
        if (cancelImport) return;   // the activity is gone
        final String err = error;
        final int count = found.size();
        ui.post(() -> {
            copying = false;
            progress.setVisibility(View.GONE);
            refresh();
            String head = err != null ? getString(R.string.setup_failed, err)
                                      : getString(mapsOnly ? R.string.setup_maps_imported
                                                           : R.string.setup_imported, count);
            status.setText(head + "\n\n" + status.getText());
            status.setVisibility(View.VISIBLE);
            detailsButton.setText("Hide setup details  −");
            if (!mapsOnly && err == null && missing(dataDir(this), true).isEmpty()) startGame();
        });
    }

    private void startGame() {
        if (!LegalNotice.accepted(this)) { LegalNotice.show(this,this::startGame); return; }
        // NEW_TASK only: RA2Activity is singleInstance, so a running game
        // (backgrounded with Home) is brought back as it was, not restarted.
        Intent i = new Intent(this, RA2Activity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(i);
          // Keep the portrait launcher behind the separate native-game task.
          // Final Quit can uncover it without an intermediate home screen.
    }
}

package org.ra2.engine;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
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
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.FrameLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
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
        String l = name.toLowerCase(Locale.ROOT);
        for (String f : FILES) if (f.toLowerCase(Locale.ROOT).equals(l)) return f;
        if (l.matches("(expand|ecache|elocal)\\d\\d\\.mix")) return l;
        if (l.equals("thememd.mix")) return l;
        if (l.matches("(movies\\d\\d|movmd\\d\\d)\\.mix")) return l;
        if (l.endsWith(".bik") && l.length() > 4) return "Movies/" + l;
        return null;
    }

    private TextView status, title, readiness;
    private ProgressBar progress;
    private Button importButton, startButton, exitButton, detailsButton;
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
        new android.app.AlertDialog.Builder(this).setTitle("Launcher appearance")
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
        root.setPadding(dp(22), dp(28), dp(22), dp(24));
        ScrollView page = new ScrollView(this);
        page.setFillViewport(true);
        page.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{theme()[0],theme()[1],theme()[0]}));
        FrameLayout centered=new FrameLayout(this);
        centered.addView(root,new FrameLayout.LayoutParams(
                Math.min(getResources().getDisplayMetrics().widthPixels,dp(600)),
                ViewGroup.LayoutParams.WRAP_CONTENT,Gravity.TOP|Gravity.CENTER_HORIZONTAL));
        page.addView(centered);

        TextView eyebrow=label("BRYLE13  /  ANDROID EDITION",11,theme()[2]);
        eyebrow.setLetterSpacing(0.12f);
        root.addView(eyebrow);
        title = label("RA2 PORT", 36, Color.rgb(244,240,223));
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        root.addView(title);
        root.addView(label("Your battlefield. Anywhere.",16,Color.rgb(160,182,193)));
        TextView hero=label("YURI'S REVENGE\n\nAllied. Soviet. Yuri.\nChoose your side. Build your legacy.",18,Color.rgb(237,226,189));
        hero.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        hero.setLineSpacing(dp(3),1f);
        hero.setPadding(dp(22),dp(24),dp(22),dp(24));
        GradientDrawable heroArt=new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{theme()[1],theme()[0],theme()[1]});
        heroArt.setCornerRadius(dp(20));heroArt.setStroke(dp(1),Color.rgb(91,110,104));
        hero.setBackground(heroArt);
        LinearLayout.LayoutParams heroSpace=new LinearLayout.LayoutParams(-1,-2);
        heroSpace.setMargins(0,dp(20),0,dp(16));root.addView(hero,heroSpace);
        readiness = label("Checking game files...", 15, Color.rgb(140, 225, 185));
        readiness.setBackground(panel(Color.rgb(20, 39, 43), Color.rgb(45, 92, 86)));
        readiness.setPadding(dp(16), dp(12), dp(16), dp(12));
        root.addView(readiness);

        startButton = button("PLAY YURI'S REVENGE", v -> startGame());
        startButton.setMinHeight(dp(72));
        startButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 19);
        startButton.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        startButton.setTextColor(Color.rgb(22,31,32));
        startButton.setBackgroundTintList(null);
        startButton.setBackground(panel(Color.rgb(224,191,115), Color.rgb(247,220,156)));
        root.addView(startButton);
        root.addView(label("Uses the game files from your own PC copy.",12,Color.rgb(135,163,181)));
        root.addView(label("YOUR GAME LIBRARY", 12, Color.rgb(135, 163, 181)));
        importButton = button("Import or update game files", v -> pickFolder());
        root.addView(importButton);
        status = label("", 14, Color.rgb(220, 230, 237));
        status.setPadding(dp(16), dp(12), dp(16), dp(12));
        status.setBackground(panel(Color.rgb(20, 31, 41), Color.rgb(44, 61, 73)));
        status.setVisibility(View.GONE);
        detailsButton=button("Game files & setup details  +",v->{
            boolean open=status.getVisibility()!=View.VISIBLE;
            status.setVisibility(open?View.VISIBLE:View.GONE);
            ((Button)v).setText(open?"Hide setup details  −":"Game files & setup details  +");
        });
        root.addView(detailsButton);root.addView(status);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        root.addView(progress);

        root.addView(label("SUPPORT & PREFERENCES",12,Color.rgb(135,163,181)));
        root.addView(button("Launcher theme",v->chooseTheme()));
        root.addView(button("Touch controls & hotkeys",v->{if(!copying)TouchSettings.show(this);}));
        root.addView(button("Game main menu",v->{
            if(copying)return;
            new android.app.AlertDialog.Builder(this).setTitle("Game main menu")
                .setSingleChoiceItems(new String[]{"Original Yuri's Revenge","Command Center (experimental)"},
                    getSharedPreferences("interface",MODE_PRIVATE).getBoolean("command_menu",false)?1:0,
                    (dialog,which)->{getSharedPreferences("interface",MODE_PRIVATE).edit()
                        .putBoolean("command_menu",which==1).apply();dialog.dismiss();})
                .setNegativeButton("Close",null).show();
        }));
        exitButton = button(getString(R.string.setup_exit), v -> finish());
        root.addView(button("Share game report", v -> Diagnostics.share(this)));
        root.addView(button("Refresh file status", v -> { if (!copying) refresh(); }));
        root.addView(button("Terms & game files",v->LegalNotice.show(this,this::refresh)));
        root.addView(button("Credits & licenses",v->LegalNotice.credits(this)));
        root.addView(exitButton);
        root.addView(label("Android fork by Bryle13. Original engine by Omar El Arab. EA has not endorsed and does not support this product.",12,Color.rgb(135,163,181)));
        root.addView(label("Uses your own PC game files. Display and touch options are available in the game's Options menu.", 12, Color.rgb(135, 163, 181)));
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
        if (!LegalNotice.accepted(this)) { LegalNotice.show(this,this::pickFolder); return; }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        // R-160: open the picker at Download/RA2 (where the instructions say
        // to put the files). Android 11+ refuses Download itself as a tree,
        // so the user must open the RA2 sub-folder and tap USE THIS FOLDER.
        if (Build.VERSION.SDK_INT >= 26) {
            i.putExtra(DocumentsContract.EXTRA_INITIAL_URI, DocumentsContract.buildDocumentUri(
                    "com.android.externalstorage.documents", "primary:Download/RA2"));
        }
        startActivityForResult(i, REQ_TREE);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_TREE || res != RESULT_OK || data == null || data.getData() == null) return;
        final Uri tree = data.getData();
        copying = true;
        cancelImport = false;
        progress.setVisibility(View.VISIBLE);
        progress.setProgress(0);
        refresh();
        importThread = new Thread(() -> importFrom(tree), "ra2-import");
        importThread.start();
    }

    /**
     * Lists the game files directly inside `docId` (case-insensitive, see
     * target()); sub-folders go to `subdirs` (document id, display name).
     */
    private void scan(ContentResolver cr, Uri tree, String docId,
                      Map<String, Uri> found, Map<String, Long> sizes, List<String[]> subdirs) {
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
                String key = target(name);
                if (key == null || found.containsKey(key)) continue;
                found.put(key, DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0)));
                sizes.put(key, c.isNull(2) ? 0L : c.getLong(2));
            }
        }
    }

    /** Copies every known file found in the picked folder (case-insensitive match). */
    private void importFrom(Uri tree) {
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
            scan(cr, tree, DocumentsContract.getTreeDocumentId(tree), found, sizes, subdirs);
            boolean anyRequired = false;
            for (int i = 0; i < REQUIRED; ++i) anyRequired |= found.containsKey(FILES[i]);
            List<String[]> deeper = new ArrayList<>();
            for (String[] sub : subdirs) {
                // W6-06-06: a Movies folder next to the files is always read;
                // R-160: other sub-folders only when the picked folder holds
                // none of the required files (the folder that CONTAINS RA2).
                if (!anyRequired || sub[1].equalsIgnoreCase("movies"))
                    scan(cr, tree, sub[0], found, sizes, anyRequired ? null : deeper);
            }
            for (String[] sub : deeper)
                if (sub[1].equalsIgnoreCase("movies")) scan(cr, tree, sub[0], found, sizes, null);
            if (found.isEmpty()) throw new java.io.IOException(getString(R.string.setup_no_files));
            for (Long sz : sizes.values()) total += sz;
            long done = 0;
            byte[] buf = new byte[1 << 20];
            for (Map.Entry<String, Uri> e : found.entrySet()) {
                if (cancelImport) throw new java.io.IOException("cancelled");
                File out = new File(dir, e.getKey());
                File tmp = new File(dir, e.getKey() + ".part");
                File parent = out.getParentFile();
                //noinspection ResultOfMethodCallIgnored
                if (parent != null) parent.mkdirs();   // Movies/
                final String name = e.getKey();
                ui.post(() -> status.setText(getString(R.string.setup_copying, name)));
                try (InputStream in = cr.openInputStream(e.getValue());
                     OutputStream os = new FileOutputStream(tmp)) {
                    if (in == null) throw new java.io.IOException("cannot open " + name);
                    int n;
                    while ((n = in.read(buf)) > 0) {
                        if (cancelImport) throw new java.io.IOException("cancelled");
                        os.write(buf, 0, n);
                        done += n;
                        final int p = total > 0 ? (int) (done * 1000 / total) : 0;
                        ui.post(() -> progress.setProgress(p));
                    }
                }
                Long want = sizes.get(name);
                if (want != null && want > 0 && tmp.length() != want)
                    throw new java.io.IOException(name + ": copied " + tmp.length() + " of " + want + " bytes");
                //noinspection ResultOfMethodCallIgnored
                out.delete();
                if (!tmp.renameTo(out)) throw new java.io.IOException("cannot rename " + tmp);
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
                                      : getString(R.string.setup_imported, count);
            status.setText(head + "\n\n" + status.getText());
            status.setVisibility(View.VISIBLE);
            detailsButton.setText("Hide setup details  −");
            if (err == null && missing(dataDir(this), true).isEmpty()) startGame();
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

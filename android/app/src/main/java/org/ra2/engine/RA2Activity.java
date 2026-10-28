package org.ra2.engine;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Rect;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import android.view.View;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import org.libsdl.app.SDLActivity;

/**
 * The game activity. SDLActivity loads libSDL2.so + libmain.so and calls
 * SDL_main -- engine/main.cpp main(), which runs run_menu() (main menu ->
 * skirmish / campaign / load). When the player quits, main() returns, SDL
 * finishes this activity and onDestroy() ends the process (W2-AR-17: no stale
 * C++ statics on the next launch).
 *
 * The launcher is SetupActivity, which checks for the user's game files and
 * offers the in-app import before starting this activity (W2-AR-03/-04).
 *
 * R-163: the game holds AUDIOFOCUS_GAIN while it is in the foreground. A call
 * or another media app taking focus silences the engine's mixer (gain 0); a
 * "may duck" loss (a navigation prompt) ducks it to 20 %; regaining focus
 * restores full volume. The gain is applied in the SDL audio callback
 * (engine/audio_sdl.cpp), so it also covers music and EVA.
 */
public class RA2Activity extends SDLActivity implements AudioManager.OnAudioFocusChangeListener {
    private static final String TAG = "RA2Activity";
    private AudioManager audio;
    private AudioFocusRequest focusRequest;   // API 26+
    private boolean haveFocus = false;

    /** engine/audio_sdl.cpp: 1 = full, 0.2 = ducked, 0 = silent. */
    private static native void nativeAudioFocusGain(float gain);

    private static void setGain(float g) {
        try {
            nativeAudioFocusGain(g);
        } catch (UnsatisfiedLinkError e) {
            // libmain.so not loaded (SDL's own error dialog is up): nothing to do.
        }
    }

    /*
     * W6-01-14: with gesture navigation, a drag from the screen edge is the
     * system's back / bars gesture -- it revealed the bars over the game and
     * scrolled the map. Exclude (Android 10+, at most 200 dp per edge, the
     * system's cap) the two edge strips a player drags on: the left edge
     * around the command column (vertically centred, engine/touch_bar.inc
     * tb_y0) and the right edge along the sidebar's cameo strip. A swipe
     * elsewhere on the edge is still the system's. The cutout mode stays the
     * platform default: in landscape the window is laid out beside a notch,
     * so the command column never sits under it.
     */
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            try {
                File mod=ModManager.selectedFolder(this);
                Os.setenv("RA2_MOD_DIR",mod==null?"":mod.getCanonicalPath(),true);
            } catch(IOException e) {
                // Never silently launch the base game when a selected profile disappeared.
                Log.e(TAG,"Selected mod unavailable",e);
                Os.setenv("RA2_MOD_DIR",new File(ModManager.root(this),".missing-selection").getAbsolutePath(),true);
            }
            SharedPreferences controls=getSharedPreferences("interface",MODE_PRIVATE);
            Os.setenv("RA2_TOUCH_ACTIONS",Integer.toString(Math.max(0,Math.min(127,controls.getInt("touch_actions",127)))),true);
            Os.setenv("RA2_TOUCH_COLLAPSED",controls.getBoolean("touch_collapsed",true)?"1":"0",true);
            String mapping=controls.getString("touch_mapping","");
            if(!mapping.isEmpty()) Os.setenv("RA2_TOUCH_MAPPING",mapping,true);
            String positions=controls.getString("touch_positions","");
            if(!positions.isEmpty()) Os.setenv("RA2_TOUCH_POSITIONS",positions,true);
            Os.setenv("RA2_TOUCH_SCALE",Integer.toString(controls.getInt("touch_scale",100)),true);
            Os.setenv("RA2_TOUCH_OPACITY",Integer.toString(controls.getInt("touch_opacity",100)),true);
            int dock=controls.getInt("touch_dock",0);
            Os.setenv("RA2_TOUCH_DOCK",dock==1?"left_side":(dock==2?"bottom_right":"bottom_left"),true);
            Os.setenv("RA2_WIDESCREEN",controls.getBoolean("widescreen_mode",true)?"1":"0",true);
            Os.setenv("RA2_FPS_LIMIT",Integer.toString(controls.getInt("fps_limit",60)),true);
            int unitHp = controls.getInt("unit_hp_mode", 1);
            int bldgHp = controls.getInt("bldg_hp_mode", 1);
            Os.setenv("RA2_UNIT_HP_MODE", Integer.toString(unitHp), true);
            Os.setenv("RA2_BLDG_HP_MODE", Integer.toString(bldgHp), true);
            int ow = controls.getInt("override_screen_width", 0);
            int oh = controls.getInt("override_screen_height", 0);
            if (ow > 0 && oh > 0) {
                Os.setenv("RA2_SCREEN_WIDTH", Integer.toString(ow), true);
                Os.setenv("RA2_SCREEN_HEIGHT", Integer.toString(oh), true);
            }
            Os.setenv("RA2_COMMAND_MENU",
                getSharedPreferences("interface",MODE_PRIVATE).getBoolean("command_menu",false)?"1":"0",true);
        } catch(android.system.ErrnoException e) { Log.w(TAG,"Menu preference unavailable",e); }
        Diagnostics.begin(this);
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 29) {
            final View root = getWindow().getDecorView();
            root.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
                @Override
                public void onLayoutChange(View v, int l, int t, int r, int b, int ol, int ot, int or, int ob) {
                    updateGestureExclusion(v);
                }
            });
        }
    }

    private void updateGestureExclusion(View v) {
        if (Build.VERSION.SDK_INT < 29) return;
        final int w = v.getWidth(), h = v.getHeight();
        if (w <= 0 || h <= 0) return;
        final float dp = getResources().getDisplayMetrics().density;
        final int strip = Math.round(48 * dp);     // wider than any gesture inset
        final int span = Math.min(h, Math.round(200 * dp));
        List<Rect> rects = new ArrayList<>();
        final int ly = (h - span) / 2;              // the command column is centred
        rects.add(new Rect(0, ly, strip, ly + span));
        final int ry = Math.min(h - span, h * 2 / 5);   // the cameo strip (below the radar)
        rects.add(new Rect(w - strip, ry, w, ry + span));
        v.setSystemGestureExclusionRects(rects);
    }

    /*
     * Multiplayer LAN discovery (engine/mp/lan_discovery.cpp, docs/gap/net/
     * 05-transport-infra.md §3.2): many Wi-Fi drivers drop broadcast frames
     * in power-save unless a MulticastLock is held. The engine holds it only
     * while the LAN browser or a LAN lobby is open (it costs battery) and
     * releases it on leave; onPause also drops it, onResume restores it.
     */
    private WifiManager.MulticastLock multicastLock;
    private boolean multicastWanted = false;

    /** Called from native code (platform_os.cpp os_android_multicast_lock). */
    public void netMulticastLock(boolean on) {
        multicastWanted = on;
        applyMulticastLock(on);
    }

    private void applyMulticastLock(boolean on) {
        try {
            if (multicastLock == null) {
                WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                if (wm == null) return;
                multicastLock = wm.createMulticastLock("ra2-lan-discovery");
                multicastLock.setReferenceCounted(false);
            }
            if (on && !multicastLock.isHeld()) multicastLock.acquire();
            if (!on && multicastLock.isHeld()) multicastLock.release();
            Log.i(TAG, "multicast lock " + (on ? "acquired" : "released"));
        } catch (RuntimeException e) {
            Log.w(TAG, "multicast lock: " + e);
        }
    }

    /*
     * W9-04-10: LAN play goes over Wi-Fi (or Ethernet). When mobile data is
     * the DEFAULT network -- a Wi-Fi without internet (a LAN-party router, a
     * friend's hotspot without data) -- Android routes a socket that is not
     * bound to a network over mobile data, so the LAN beacons, the join and
     * the match never reach the other phones. While the LAN pages (browser,
     * lobby, match) are in use the engine binds the process to the Wi-Fi
     * network: every socket created from then on (the lobby's / match's UDP
     * socket) uses it. The Internet page unbinds (the server is reached over
     * the default network). Called from native code (platform_os.cpp
     * os_android_bind_lan).
     * Returns 0 no Wi-Fi / Ethernet network, 1 it is already the default
     * network, 2 bound to it (mobile data stays the default).
     */
    public int netBindLan(boolean on) {
        if (Build.VERSION.SDK_INT < 23) return 1;
        try {
            ConnectivityManager cm = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) return 0;
            if (!on) {
                cm.bindProcessToNetwork(null);
                Log.i(TAG, "LAN: process network binding cleared");
                return 1;
            }
            Network lan = null;
            for (Network n : cm.getAllNetworks()) {
                NetworkCapabilities c = cm.getNetworkCapabilities(n);
                if (c == null) continue;
                if (c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
                    c.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                    lan = n;
                    break;
                }
            }
            if (lan == null) {
                cm.bindProcessToNetwork(null);
                Log.i(TAG, "LAN: no Wi-Fi / Ethernet network");
                return 0;
            }
            if (lan.equals(cm.getActiveNetwork())) {
                cm.bindProcessToNetwork(null);
                return 1;
            }
            final boolean ok = cm.bindProcessToNetwork(lan);
            Log.i(TAG, "LAN: mobile data is the default network; process bound to Wi-Fi: " + ok);
            return ok ? 2 : 0;
        } catch (RuntimeException e) {
            Log.w(TAG, "LAN bind: " + e);
            return 0;
        }
    }

    @Override
    protected void onPause() {
        if (multicastWanted) applyMulticastLock(false);
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        requestFocus();
        if (multicastWanted) applyMulticastLock(true);
    }

    /** engine/platform_os.cpp: keep AUTOSAVE.SAV through the scene teardown. */
    private static native void nativeKeepAutosave();

    @Override
    public void finish() {
        if (!isFinishing()) {
            Intent launcher = new Intent(this, SetupActivity.class);
            launcher.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            startActivity(launcher);
        }
        super.finish();
    }

    @Override
    protected void onDestroy() {
        // W6-01-10: destroyed without the player quitting (a configuration
        // change the activity cannot take in place, e.g. switching to gesture
        // navigation or an overlay/theme change): the engine is about to tear
        // the running game down; its background autosave must survive so the
        // next start offers RESUME instead of deleting it.
        if (!isFinishing()) {
            Log.i(TAG, "onDestroy without finishing (activity restart): keeping the autosave");
            try {
                nativeKeepAutosave();
            } catch (UnsatisfiedLinkError e) {
                // libmain.so not loaded: no game is running.
            }
        }
        abandonFocus();
        super.onDestroy();   // SDLActivity joins the SDL (engine) thread here
        // W2-AR-17: the engine keeps process-wide state in C++ statics; once
        // the player has quit, end the process so the next launch re-enters
        // main() with fresh statics instead of reusing this process. Done
        // after SDL's own teardown (an exit() from the SDL thread crashed).
        if (isFinishing()) {
            Diagnostics.finish(this);
            // Setup already resumed through finish(); only the native process
            // ends here, after SDL teardown and diagnostic completion.
            android.os.Process.killProcess(android.os.Process.myPid());
        }
    }

    private void requestFocus() {
        if (haveFocus) return;
        if (audio == null) audio = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        if (audio == null) return;
        int r;
        if (Build.VERSION.SDK_INT >= 26) {
            if (focusRequest == null) {
                AudioAttributes attrs = new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build();
                focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                        .setAudioAttributes(attrs)
                        .setWillPauseWhenDucked(false)
                        .setOnAudioFocusChangeListener(this)
                        .build();
            }
            r = audio.requestAudioFocus(focusRequest);
        } else {
            r = requestFocusLegacy();
        }
        haveFocus = r == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        Log.i(TAG, "audio focus request: " + (haveFocus ? "granted" : "refused (" + r + ")"));
        if (haveFocus) setGain(1.0f);
    }

    @SuppressWarnings("deprecation")
    private int requestFocusLegacy() {
        return audio.requestAudioFocus(this, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN);
    }

    @SuppressWarnings("deprecation")
    private void abandonFocus() {
        if (audio == null || !haveFocus) return;
        if (Build.VERSION.SDK_INT >= 26 && focusRequest != null) audio.abandonAudioFocusRequest(focusRequest);
        else audio.abandonAudioFocus(this);
        haveFocus = false;
    }

    @Override
    public void onAudioFocusChange(int change) {
        Log.i(TAG, "audio focus change " + change);
        switch (change) {
            case AudioManager.AUDIOFOCUS_GAIN:
                haveFocus = true;
                setGain(1.0f);
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                setGain(0.2f);
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                setGain(0.0f);
                break;
            case AudioManager.AUDIOFOCUS_LOSS:
                // Permanent loss (another media app): stay silent until the
                // game is resumed, which requests focus again.
                haveFocus = false;
                setGain(0.0f);
                break;
            default:
                break;
        }
    }

    /**
     * Called from native code (the engine's missing-files page, IMPORT
     * button) on the SDL thread: open the importer. startActivity() is a
     * synchronous binder call and SetupActivity lives in its own process, so
     * the engine quitting right after this does not lose or kill it.
     */
    public void openImporter() {
        Intent i = new Intent(this, SetupActivity.class);
        i.putExtra(SetupActivity.EXTRA_IMPORT, true);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(i);
    }

    /** Called by the pause menu on SDL's thread; Android UI stays on its UI thread. */
    public void shareGameReport() {
        runOnUiThread(() -> { if (!isFinishing()) Diagnostics.share(this); });
    }
}

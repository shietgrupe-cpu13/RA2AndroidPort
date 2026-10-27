package org.ra2.engine;
// Android fork additions by Bryle13; project 0BSD license.

import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.widget.CheckBox;

/** Local presentation preferences; no game rules or saved simulation change. */
final class TouchSettings {
    static void show(Context context) {
        SharedPreferences p=context.getSharedPreferences("interface",Context.MODE_PRIVATE);
        String[] labels={"Stop — S","Guard — G","Deploy / Unload — D","Scatter — X",
            "Force Fire — Ctrl","Attack Move — Ctrl + Shift","Add selection — Shift"};
        int mask=p.getInt("touch_actions",127);
        boolean[] checked=new boolean[labels.length];
        for(int i=0;i<checked.length;++i)checked[i]=(mask&(1<<i))!=0;
        CheckBox collapsed=new CheckBox(context);
        collapsed.setText("Start command panel collapsed");
        collapsed.setChecked(p.getBoolean("touch_collapsed",true));
        new AlertDialog.Builder(context).setTitle("Yuri's Revenge touch controls")
            .setMultiChoiceItems(labels,checked,(dialog,which,on)->checked[which]=on)
            .setView(collapsed)
            .setPositiveButton("Save",(dialog,which)->{
                int selected=0;for(int i=0;i<checked.length;++i)if(checked[i])selected|=1<<i;
                p.edit().putInt("touch_actions",selected).putBoolean("touch_collapsed",collapsed.isChecked()).apply();
            })
            .setNeutralButton("Hotkey guide",(dialog,which)->guide(context))
            .setNegativeButton("Cancel",null).show();
    }
    private static void guide(Context context) {
        new AlertDialog.Builder(context).setTitle("Commands & gestures")
            .setMessage("Changes apply on your next game launch. MORE and control groups remain available.\n\n"+
                "MORE → ROUTE Z: enter waypoint mode, tap route points, then tap ROUTE Z again to start. X on the mode banner cancels the uncommitted route. Only movement routes are supported.\n\n"+
                "Stop (S) clears a committed route. A new normal order replaces it.\n\n"+
                "D: Deploy, pack or unload the selected unit.\nG: Guard. X: Scatter.\n"+
                "Ctrl: Force Fire. Ctrl + Shift: Attack Move.\n"+
                "Double tap: select matching units on screen.\nPinch: zoom. Two-finger tap: cancel.\n"+
                "Tap group: select. Hold group: assign.\n\n"+
                "Keyboard bindings can be changed in the game's Keyboard options; the labels above show defaults.")
            .setPositiveButton("OK",null).show();
    }
}

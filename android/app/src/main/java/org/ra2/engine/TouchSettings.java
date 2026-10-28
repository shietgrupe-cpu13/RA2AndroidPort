package org.ra2.engine;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.FrameLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.List;

/** Edits a draft; only Save commits preferences used by the next game session. */
final class TouchSettings {
    private static int dp(Context c,float n) { return Math.round(c.getResources().getDisplayMetrics().density*n); }
    private static TextView label(Context c,String text) {
        TextView v=new TextView(c);v.setText(text);v.setTextSize(14);v.setTextColor(Color.rgb(220,232,235));
        v.setPadding(dp(c,8),dp(c,6),dp(c,8),dp(c,6));return v;
    }
    private static Button button(Context c,String text,Runnable action) {
        Button v=new Button(c);v.setText(text);v.setMinHeight(dp(c,48));v.setOnClickListener(w->action.run());return v;
    }
    private static Spinner spinner(Context c,String[] values,int selection) {
        Spinner v=new Spinner(c);v.setAdapter(new ArrayAdapter<>(c,android.R.layout.simple_spinner_dropdown_item,values));
        v.setSelection(Math.max(0,Math.min(values.length-1,selection)));v.setMinimumHeight(dp(c,48));return v;
    }
    private static LinearLayout row(Context c) { LinearLayout v=new LinearLayout(c);v.setOrientation(LinearLayout.HORIZONTAL);return v; }
    private static void weighted(LinearLayout row,View child) {row.addView(child,new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1));}
    static void show(Context context) {
        SharedPreferences prefs=context.getSharedPreferences("interface",Context.MODE_PRIVATE);
        List<TouchLayout.Item> layout=TouchLayout.parse(prefs.getString("touch_mapping",""),prefs.getString("touch_positions",""),prefs.getInt("touch_actions",127),prefs.getInt("touch_layout_version",1));
        if(prefs.getBoolean("touch_free_layout",true))TouchLayout.ensurePositions(layout);
        LinearLayout root=new LinearLayout(context);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(context,12),0,dp(context,12),0);
        root.addView(label(context,"Drag controls in the landscape editor. Hide unwanted buttons and restore them later. Save applies on the next game launch."));
        LinearLayout choices=row(context);
        Spinner preset=spinner(context,new String[]{"Keep saved layout","Free screen layout","Classic dock","Two-thumb layout","Yuri controls"},0);
        int[] sizeValues={50,65,80,100,120,140};int sizeIndex=3;
        for(int i=0;i<sizeValues.length;i++)if(sizeValues[i]==prefs.getInt("touch_scale",100))sizeIndex=i;
        Spinner scale=spinner(context,new String[]{"Size: 50%","Size: 65%","Size: 80%","Size: 100%","Size: 120%","Size: 140%"},sizeIndex);
        int[] opValues={100,85,70,50};int op=0;for(int i=0;i<opValues.length;i++)if(opValues[i]==prefs.getInt("touch_opacity",100))op=i;
        Spinner opacity=spinner(context,new String[]{"Opacity: 100%","Opacity: 85%","Opacity: 70%","Opacity: 50%"},op);
        weighted(choices,preset);weighted(choices,scale);weighted(choices,opacity);root.addView(choices);
        Spinner dock=spinner(context,new String[]{"Dock: bottom left","Dock: left edge","Dock: bottom right"},prefs.getInt("touch_dock",0));root.addView(dock);
        CheckBox free=new CheckBox(context);free.setText("Free screen controls (no large dock panel)");free.setTextColor(Color.WHITE);free.setChecked(prefs.getBoolean("touch_free_layout",true));root.addView(free);
        CheckBox collapsed=new CheckBox(context);collapsed.setText("Start with controls folded (tap arrow to show)");collapsed.setTextColor(Color.WHITE);collapsed.setChecked(prefs.getBoolean("touch_collapsed",true));collapsed.setMinHeight(dp(context,48));root.addView(collapsed);
        collapsed.setEnabled(!free.isChecked());dock.setEnabled(!free.isChecked());
        free.setOnCheckedChangeListener((v,on)->{collapsed.setEnabled(!on);dock.setEnabled(!on);});
        LinearLayout rows=new LinearLayout(context);rows.setOrientation(LinearLayout.VERTICAL);Runnable[] refresh=new Runnable[1];
        refresh[0]=()->{
            rows.removeAllViews();
            for(TouchLayout.Item item:layout) {
                LinearLayout r=row(context);CheckBox enabled=new CheckBox(context);enabled.setText(item.label);enabled.setTextColor(Color.WHITE);enabled.setMinHeight(dp(context,48));enabled.setChecked(item.visible);
                enabled.setOnCheckedChangeListener((v,checked)->{
                    item.visible=checked;
                    if(checked && item.x<0){int slot=layout.indexOf(item);item.x=55+(slot%3)*20;item.y=5+(slot%8)*12;}
                });weighted(r,enabled);
                r.addView(button(context,"Place",()->showEditor(context,layout,item.id,sizeValues[scale.getSelectedItemPosition()],()->refresh[0].run())));rows.addView(r);
            }
        };
        preset.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            public void onNothingSelected(AdapterView<?> parent) { }
            public void onItemSelected(AdapterView<?> parent,View view,int position,long id) {
                if(position==0)return;TouchLayout.preset(layout,position==1?3:position-2);free.setChecked(position!=2);refresh[0].run();preset.setSelection(0);
            }
        });
        root.addView(button(context,"Edit positions in landscape",()->showEditor(context,layout,null,sizeValues[scale.getSelectedItemPosition()],()->refresh[0].run())));
        root.addView(button(context,"Commands and Yuri controls guide",()->guide(context)));root.addView(rows);refresh[0].run();ScrollView scroll=new ScrollView(context);scroll.addView(root);
        new AlertDialog.Builder(context).setTitle("Touch controls").setView(scroll)
            .setPositiveButton("Save",(d,which)->prefs.edit().putInt("touch_layout_version",TouchLayout.VERSION).putInt("touch_actions",TouchLayout.mask(layout))
                .putString("touch_mapping",TouchLayout.mapping(layout)).putString("touch_positions",TouchLayout.positions(layout))
                .putBoolean("touch_free_layout",free.isChecked()).putInt("touch_scale",sizeValues[scale.getSelectedItemPosition()]).putInt("touch_opacity",opValues[opacity.getSelectedItemPosition()])
                .putInt("touch_dock",dock.getSelectedItemPosition()).putBoolean("touch_collapsed",collapsed.isChecked()).apply())
            .setNegativeButton("Cancel",null).show();
    }
    private static void showEditor(Context c,List<TouchLayout.Item> original,String selected,int globalSize,Runnable changed) {
        List<TouchLayout.Item> draft=TouchLayout.copy(original);TouchLayout.ensurePositions(draft);
        if(selected!=null)for(TouchLayout.Item item:draft)if(item.id.equals(selected))item.visible=true;
        Dialog dialog=new Dialog(c,android.R.style.Theme_Black_NoTitleBar_Fullscreen);
        dialog.getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,WindowManager.LayoutParams.FLAG_FULLSCREEN);
        FrameLayout root=new FrameLayout(c);root.setBackgroundColor(Color.rgb(14,23,25));
        TextView status=label(c,"Drag any button anywhere. Select it, then Size to resize. Tools hides this toolbar.");
        DragCanvas canvas=new DragCanvas(c,draft,status,selected,globalSize);root.addView(canvas,new FrameLayout.LayoutParams(-1,-1));
        FrameLayout.LayoutParams help=new FrameLayout.LayoutParams(-1,-2,android.view.Gravity.TOP);help.rightMargin=dp(c,70);root.addView(status,help);
        LinearLayout controls=row(c);controls.setBackgroundColor(0xcc142523);
        weighted(controls,button(c,"Hide",canvas::hideSelected));
        weighted(controls,button(c,"Restore",()->{
            String[] labels=new String[draft.size()];boolean[] checks=new boolean[draft.size()];for(int i=0;i<draft.size();i++){labels[i]=draft.get(i).label;checks[i]=draft.get(i).visible;}
            new AlertDialog.Builder(c).setTitle("Show / hide controls").setMultiChoiceItems(labels,checks,(d,i,on)->{draft.get(i).visible=on;canvas.invalidate();}).setPositiveButton("Done",null).show();
        }));
        weighted(controls,button(c,"Size",canvas::resizeSelected));
        weighted(controls,button(c,"Reset",()->{TouchLayout.preset(draft,3);canvas.invalidate();}));
        weighted(controls,button(c,"Use layout",()->{original.clear();original.addAll(TouchLayout.copy(draft));changed.run();dialog.dismiss();}));
        weighted(controls,button(c,"Cancel",dialog::dismiss));
        root.addView(controls,new FrameLayout.LayoutParams(-1,-2,android.view.Gravity.BOTTOM));
        Button tools=button(c,"Tools",()->{int visibility=controls.getVisibility()==View.VISIBLE?View.GONE:View.VISIBLE;controls.setVisibility(visibility);status.setVisibility(visibility);});
        root.addView(tools,new FrameLayout.LayoutParams(dp(c,70),dp(c,44),android.view.Gravity.TOP|android.view.Gravity.RIGHT));
        dialog.setContentView(root);dialog.show();dialog.getWindow().setLayout(-1,-1);
        dialog.getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY|View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }
    private static final class DragCanvas extends View {
        final List<TouchLayout.Item> items;final TextView status;final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);final int globalSize;
        final List<TouchLayout.Item> drawn=new ArrayList<>();final List<RectF> rects=new ArrayList<>();
        TouchLayout.Item selected,dragged;float offsetX,offsetY,dragW,dragH;
        DragCanvas(Context c,List<TouchLayout.Item> items,TextView status,String id,int globalSize) {
            super(c);this.items=items;this.status=status;this.globalSize=globalSize;setContentDescription("Full screen touch layout editor");setFocusable(true);
            for(TouchLayout.Item item:items)if(item.id.equals(id))selected=item;
        }
        void hideSelected(){if(selected!=null){selected.visible=false;status.setText(selected.label+" hidden. Restore keeps its position and size.");invalidate();}}
        void resizeSelected(){if(selected!=null){selected.size=selected.size>=160?40:selected.size+20;status.setText(selected.label+" size: "+selected.size+"%");invalidate();}}
        public boolean performClick(){super.performClick();return true;}
        protected void onDraw(Canvas c) {
            super.onDraw(c);paint.setColor(Color.rgb(23,43,38));c.drawRect(0,0,getWidth(),getHeight(),paint);paint.setColor(Color.rgb(40,65,56));
            for(float x=0;x<getWidth();x+=dp(getContext(),32))c.drawLine(x,0,x,getHeight(),paint);
            for(float y=0;y<getHeight();y+=dp(getContext(),32))c.drawLine(0,y,getWidth(),y,paint);
            drawn.clear();rects.clear();
            for(TouchLayout.Item item:items) {
                if(!item.visible)continue;
                int[] b=TouchLayout.bounds(item,getWidth(),getHeight(),getResources().getDisplayMetrics().density,globalSize);
                RectF r=new RectF(b[0],b[1],b[0]+b[2],b[1]+b[3]);drawn.add(item);rects.add(r);
                paint.setStyle(Paint.Style.FILL);paint.setColor(item==selected?Color.rgb(70,110,95):Color.rgb(34,61,59));c.drawRoundRect(r,6,6,paint);
                paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(getContext(),1));paint.setColor(Color.rgb(130,218,175));c.drawRoundRect(r,6,6,paint);
                paint.setStyle(Paint.Style.FILL);paint.setColor(Color.WHITE);paint.setTextAlign(Paint.Align.CENTER);paint.setTextSize(Math.min(r.height()/3,r.width()/Math.max(5,item.id.length())*1.5f));
                c.drawText(item.id,r.centerX(),r.centerY()-(paint.ascent()+paint.descent())/2,paint);
            }
        }
        public boolean onTouchEvent(MotionEvent event) {
            float x=event.getX(),y=event.getY();
            switch(event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    dragged=null;for(int i=rects.size()-1;i>=0;i--)if(rects.get(i).contains(x,y)) {
                        selected=dragged=drawn.get(i);RectF r=rects.get(i);offsetX=x-r.left;offsetY=y-r.top;dragW=r.width();dragH=r.height();status.setText(selected.label+" - drag anywhere; Size changes this button");break;
                    }getParent().requestDisallowInterceptTouchEvent(true);invalidate();return true;
                case MotionEvent.ACTION_MOVE:
                    if(dragged!=null){dragged.x=percent(x-offsetX,getWidth()-dragW);dragged.y=percent(y-offsetY,getHeight()-dragH);invalidate();}return true;
                case MotionEvent.ACTION_UP:dragged=null;performClick();return true;
                case MotionEvent.ACTION_CANCEL:dragged=null;return true;
                default:return true;
            }
        }
        private float percent(float value,float range){return range<=0?0:Math.max(0,Math.min(100,value/range*100));}
    }
    private static void guide(Context c) {
        new AlertDialog.Builder(c).setTitle("Commands and Yuri controls").setMessage(
            "Save in Touch controls, then launch the game. Hidden controls keep their positions. Cancel discards edits.\n\n"+
            "Drag pans the map; hold then drag selects a box. Pinch zooms; two-finger tap cancels. Tap a group to select; hold to assign. MORE opens secondary commands.\n\n"+
            "Psi Wave deploys eligible selected psychic infantry. Slave Miner deploy / pack acts on selected miners only.\n"+
            "Psychic Reveal, Dominator and Genetic Mutator become available when their power is ready. Tap the shortcut, then a target on the battlefield.\n"+
            "Mind control, Magnetron and Disc drain use normal target taps with the appropriate unit selected.\n\n"+
            "Force Fire and Attack Move arm the next target tap. Deploy / unload also works for other factions.").setPositiveButton("OK",null).show();
    }
}

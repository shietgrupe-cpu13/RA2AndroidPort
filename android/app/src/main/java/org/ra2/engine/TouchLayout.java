package org.ra2.engine;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/** Saved layout data, independent of Android views so migration can be checked on the JVM. */
final class TouchLayout {
    static final int VERSION = 2;
    static final class Item {
        final String id, label;
        final int bit;
        boolean visible;
        float x = -1, y = -1;
        int size = 100;
        Item(String id, String label, int bit, boolean visible) {
            this.id=id; this.label=label; this.bit=bit; this.visible=visible;
        }
        Item copy() { Item out=new Item(id,label,bit,visible); out.x=x; out.y=y; out.size=size; return out; }
    }
    static List<Item> defaults() {
        List<Item> out=new ArrayList<>();
        String[][] actions={
            {"STOP","Stop"},{"GUARD","Guard"},{"DEPLOY","Deploy / unload"},
            {"SCATTER","Scatter"},{"FIRE","Force fire"},{"MOVE","Attack move"},
            {"ADD","Add selection"},{"MORE","More commands"},{"BOX","Box selection"},
            {"ROUTE","Waypoint route"},{"HOME","Home base"},{"TYPE","Select matching units"},
            {"NEXT","Next unit"},{"PREV","Previous unit"},{"EVENT","Last event"},
            {"FOLLOW","Follow selection"},{"BRIEF","Mission briefing"},
            {"PSI","Yuri: Psi Wave"},{"MINER","Yuri: Slave Miner deploy / pack"},
            {"REVEAL","Yuri: Psychic Reveal"},{"DOMINATOR","Yuri: Psychic Dominator"},
            {"MUTATOR","Yuri: Genetic Mutator"}};
        for(int i=0;i<actions.length;i++) out.add(new Item(actions[i][0],actions[i][1],i<7?i:-1,i<8));
        for(int i=1;i<=10;i++) out.add(new Item("GROUP"+(i%10),"Control group "+(i%10),-1,i<=5));
        return out;
    }
    static List<Item> copy(List<Item> src) {
        List<Item> out=new ArrayList<>(); for(Item item:src) out.add(item.copy()); return out;
    }
    static List<Item> parse(String mapping,String positions,int mask,int version) {
        List<Item> catalog=defaults(),out=new ArrayList<>();
        Map<String,Item> byId=new HashMap<>(); for(Item item:catalog) byId.put(item.id,item);
        Set<String> seen=new HashSet<>();
        boolean explicit=mapping!=null && !mapping.trim().isEmpty();
        if(explicit) for(String id:mapping.split(",")) {
            Item item=byId.get(id.trim());
            if(item!=null && seen.add(item.id)) {item.visible=true;out.add(item);}
        }
        for(Item item:catalog) if(!seen.contains(item.id)) {
            item.visible=explicit ? false : item.bit>=0 && (mask&(1<<item.bit))!=0;
            if(!explicit && (item.id.equals("MORE") || item.id.matches("GROUP[1-5]"))) item.visible=true;
            // Old native layouts always appended MORE and groups 1..5.
            if(explicit && version<VERSION && (item.id.equals("MORE") || item.id.matches("GROUP[1-5]"))) item.visible=true;
            out.add(item);
        }
        if(positions!=null) for(String entry:positions.split(";")) {
            String[] fields=entry.split(":"); if(fields.length!=2) continue;
            Item item=byId.get(fields[0]); String[] xy=fields[1].split(",");
            if(item==null || xy.length<2) continue;
            try {
                float x=Float.parseFloat(xy[0]),y=Float.parseFloat(xy[1]);
                if(Float.isNaN(x)||Float.isInfinite(x)||Float.isNaN(y)||Float.isInfinite(y)) continue;
                item.x=Math.max(0,Math.min(100,x));item.y=Math.max(0,Math.min(100,y));
                if(xy.length>2)item.size=Math.max(40,Math.min(180,Integer.parseInt(xy[2])));
            } catch(NumberFormatException ignored) { }
        }
        return out;
    }
    static String mapping(List<Item> items) {
        StringBuilder out=new StringBuilder();
        for(Item item:items) if(item.visible) {if(out.length()>0)out.append(',');out.append(item.id);}
        return out.length()==0?"NONE":out.toString();
    }
    static String positions(List<Item> items) {
        StringBuilder out=new StringBuilder();
        // Hidden controls retain their positions for later restoration.
        for(Item key:defaults()) {
            Item item=null;for(Item candidate:items)if(candidate.id.equals(key.id)){item=candidate;break;}
            if(item==null || item.x<0 || item.y<0)continue;
            if(out.length()>0)out.append(';');
            out.append(String.format(Locale.US,"%s:%.2f,%.2f,%d",item.id,item.x,item.y,item.size));
        }
        return out.toString();
    }
    static int mask(List<Item> items) {
        int mask=0;for(Item item:items)if(item.visible && item.bit>=0)mask|=1<<item.bit;return mask;
    }
    // Percentages describe the remaining travel after the button's own size.
    static void ensurePositions(List<Item> items) {
        int extra=0;
        for(Item item:items) {
            if(item.x>=0 && item.y>=0)continue;
            switch(item.id) {
                case "FIRE":item.x=42;item.y=4;break;
                case "MOVE":item.x=54;item.y=4;break;
                case "DEPLOY":item.x=50;item.y=92;break;
                case "STOP":item.x=6;item.y=75;break;
                case "GUARD":item.x=18;item.y=75;break;
                case "SCATTER":item.x=30;item.y=75;break;
                case "ADD":item.x=80;item.y=75;break;
                case "MORE":item.x=92;item.y=92;break;
                default:
                    if(item.id.matches("GROUP[1-5]")) {item.x=2;item.y=6+(item.id.charAt(5)-'1')*13;}
                    else {item.x=64+(extra%3)*12;item.y=20+(extra/3)*10;extra++;}
            }
        }
    }
    static int[] bounds(Item item,int width,int height,float dpScale,int globalSize) {
        float f=dpScale*globalSize/100f*item.size/100f;
        int w=Math.min(width,Math.max(1,Math.round(64*f))),h=Math.min(height,Math.max(1,Math.round(36*f)));
        return new int[]{Math.round(item.x*(width-w)/100),Math.round(item.y*(height-h)/100),w,h};
    }
    static void preset(List<Item> items,int preset) {
        Set<String> active=new HashSet<>(Arrays.asList("STOP","GUARD","DEPLOY","SCATTER","FIRE","MOVE","ADD","MORE","GROUP1","GROUP2","GROUP3","GROUP4","GROUP5"));
        if(preset==2)active.addAll(Arrays.asList("PSI","MINER","REVEAL","DOMINATOR","MUTATOR"));
        int i=0;
        for(Item item:items) {
            item.visible=active.contains(item.id);item.x=item.y=-1;item.size=100;
            if(preset>0 && preset<3 && item.visible) {
                int slot=i++;boolean right=slot>=9;int row=slot%9;
                item.x=right?78:2;item.y=5+row*10;
            }
        }
        if(preset>0)ensurePositions(items);
    }
}

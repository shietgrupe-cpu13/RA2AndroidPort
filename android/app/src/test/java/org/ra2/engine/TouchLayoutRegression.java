package org.ra2.engine;
import java.util.List;

/** Standalone JVM regressions for saved controls; does not require an emulator. */
public final class TouchLayoutRegression {
    static TouchLayout.Item find(List<TouchLayout.Item> items,String id) {
        for(TouchLayout.Item i:items)if(i.id.equals(id))return i;throw new AssertionError(id);
    }
    static void check(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args) {
        List<TouchLayout.Item> old=TouchLayout.parse("STOP,DEPLOY,STOP,UNKNOWN","STOP:17.5,22;DEPLOY:NaN,9",127,1);
        check(find(old,"STOP").visible && find(old,"MORE").visible && find(old,"GROUP5").visible);
        check(old.size()==TouchLayout.defaults().size());check(find(old,"STOP").x==17.5f);
        check(find(old,"DEPLOY").x==-1);
        find(old,"STOP").visible=false;
        List<TouchLayout.Item> loaded=TouchLayout.parse(TouchLayout.mapping(old),TouchLayout.positions(old),0,2);
        check(!find(loaded,"STOP").visible && find(loaded,"STOP").x==17.5f);
        find(loaded,"STOP").visible=true;check(find(loaded,"STOP").y==22);
        List<TouchLayout.Item> draft=TouchLayout.copy(loaded);find(draft,"STOP").x=99;
        check(find(loaded,"STOP").x==17.5f);
        for(TouchLayout.Item item:loaded)item.visible=false;
        check(TouchLayout.mapping(loaded).equals("NONE"));
        for(TouchLayout.Item item:TouchLayout.parse("NONE",TouchLayout.positions(loaded),127,2))check(!item.visible);
        TouchLayout.preset(draft,2);
        for(String id:new String[]{"PSI","MINER","REVEAL","DOMINATOR","MUTATOR"})check(find(draft,id).visible);
        List<TouchLayout.Item> roundtrip=TouchLayout.parse(TouchLayout.mapping(draft),TouchLayout.positions(draft),0,2);
        check(TouchLayout.mapping(roundtrip).equals(TouchLayout.mapping(draft)));
        check(TouchLayout.positions(roundtrip).equals(TouchLayout.positions(draft)));
        List<TouchLayout.Item> bad=TouchLayout.parse("STOP","STOP:Infinity,4;MINER:-40,180",0,2);
        check(find(bad,"STOP").x==-1);check(find(bad,"MINER").x==0 && find(bad,"MINER").y==100);
        TouchLayout.preset(draft,3);
        check(find(draft,"FIRE").x>35 && find(draft,"FIRE").y<10);
        check(find(draft,"MOVE").x>50 && find(draft,"MOVE").y<10);
        check(find(draft,"DEPLOY").x==50 && find(draft,"DEPLOY").y>90);
        TouchLayout.Item corner=find(draft,"DEPLOY");corner.x=100;corner.y=100;corner.size=60;
        int[] rect=TouchLayout.bounds(corner,2400,1080,3,100);
        check(rect[0]+rect[2]==2400 && rect[1]+rect[3]==1080);
        check(rect[3]==65); // Compact 36dp button at 60%, not the old 48dp dock.
        List<TouchLayout.Item> resized=TouchLayout.parse(TouchLayout.mapping(draft),TouchLayout.positions(draft),0,2);
        check(find(resized,"DEPLOY").size==60);
        find(resized,"DEPLOY").visible=false;
        List<TouchLayout.Item> restored=TouchLayout.parse(TouchLayout.mapping(resized),TouchLayout.positions(resized),0,2);
        check(find(restored,"DEPLOY").size==60 && find(restored,"DEPLOY").x==100);
        System.out.println("Touch layout migration, hide/restore, cancel, presets and roundtrip passed");
    }
}

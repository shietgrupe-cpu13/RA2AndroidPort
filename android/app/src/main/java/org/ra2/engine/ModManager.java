package org.ra2.engine;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Intent;
import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Isolated, opt-in data profiles. The native scanner is the activation authority. */
final class ModManager {
    private static native String nativeReport(String path);
    private static volatile boolean importing;
    private static volatile boolean cancelRequested;
    private static boolean loaded;
    private static synchronized void loadScanner() {
        if (!loaded) { System.loadLibrary("SDL2"); System.loadLibrary("main"); loaded=true; }
    }
    static File root(Activity a) { File f=new File(SetupActivity.dataDir(a),"mods"); f.mkdirs(); return f; }
    static String selected(Activity a) {return a.getSharedPreferences("mods",0).getString("selected","");}
    static File selectedFolder(Activity a) throws IOException {
        String id=selected(a);if(id.isEmpty())return null;
        if(!id.matches("[a-z0-9][a-z0-9_-]{0,63}"))throw new IOException("Invalid mod selection");
        File base=root(a).getCanonicalFile(), f=new File(base,id).getCanonicalFile();
        if(!f.getParentFile().equals(base)||!f.isDirectory())throw new IOException("Selected mod is missing");
        return f;
    }
    static void show(Activity a,int request) {
        if(importing){Toast.makeText(a,"A mod import is still running",Toast.LENGTH_SHORT).show();return;}
        File[] folders=root(a).listFiles(f->f.isDirectory()&&!f.getName().startsWith("."));
        if(folders==null)folders=new File[0];Arrays.sort(folders,(left,right)->left.getName().compareTo(right.getName()));
        final File[] mods=folders;String[] rows=new String[mods.length+2];
        rows[0]="Base game"+(selected(a).isEmpty()?" · selected":"");rows[1]="Import mod folder…";
        for(int i=0;i<mods.length;i++)rows[i+2]=a.getSharedPreferences("mods",0).getString("name."+mods[i].getName(),mods[i].getName())+(selected(a).equals(mods[i].getName())?" · selected":"");
        new AlertDialog.Builder(a).setTitle("Mods · applies after quitting the running game")
            .setItems(rows,(d,n)->{
                if(n==0){a.getSharedPreferences("mods",0).edit().remove("selected").apply();return;}
                if(n==1){new AlertDialog.Builder(a).setTitle("Import mod").setItems(new String[]{"Choose mod folder","Choose ZIP archive"},(choice,kind)->{
                    Intent pick=new Intent(kind==0?Intent.ACTION_OPEN_DOCUMENT_TREE:Intent.ACTION_OPEN_DOCUMENT);
                    if(kind==1){pick.addCategory(Intent.CATEGORY_OPENABLE);pick.setType("application/zip");}
                    pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);a.startActivityForResult(pick,kind==0?request:6);
                }).setNegativeButton("Cancel",null).show();return;}
                inspect(a,mods[n-2]);
            }).setNegativeButton("Close",null).show();
    }
    private static void inspect(Activity a,File folder) {
        Toast.makeText(a,"Checking mod requirements…",Toast.LENGTH_SHORT).show();
        new Thread(()->{
            String report;
            try{loadScanner();report=nativeReport(folder.getCanonicalPath());}
            catch(Exception|LinkageError e){report="BLOCKED\nCould not inspect mod: "+e.getMessage();}
            for(String line:report.split("\n"))if(line.startsWith("Name: ")){
                a.getSharedPreferences("mods",0).edit().putString("name."+folder.getName(),line.substring(6)).apply();break;
            }
            try(OutputStream saved=new FileOutputStream(new File(Diagnostics.dir(a),"mod-inspection.txt"))){saved.write(report.getBytes(StandardCharsets.UTF_8));}
            catch(IOException ignored){}
            final String result=report;
            a.runOnUiThread(()->{
                if(a.isFinishing()||a.isDestroyed())return;
                TextView body=new TextView(a);body.setText(result);body.setPadding(24,16,24,16);body.setTextIsSelectable(true);
                ScrollView scroll=new ScrollView(a);scroll.addView(body);
                AlertDialog.Builder dialog=new AlertDialog.Builder(a).setTitle("Mod compatibility").setView(scroll).setNegativeButton("Close",null);
                if(!result.startsWith("BLOCKED"))dialog.setPositiveButton(result.startsWith("PARTIAL")?"Enable experimental mod":"Enable mod",(d,n)->{
                    a.getSharedPreferences("mods",0).edit().putString("selected",folder.getName()).apply();
                    Toast.makeText(a,"Selected. Quit any running game before playing this profile.",Toast.LENGTH_LONG).show();
                });
                dialog.show();
            });
        },"ra2-mod-check").start();
    }
    static void importFolder(Activity a,Uri tree) {
        if(importing)return;importing=true;
        Toast.makeText(a,"Importing mod into its own folder…",Toast.LENGTH_LONG).show();
        new Thread(()->{
            File staging=new File(root(a),".import-"+UUID.randomUUID());File completed=null;String error=null;
            try{
                if(!staging.mkdir())throw new IOException("Cannot create mod import folder");
                copyTree(a,tree,DocumentsContract.getTreeDocumentId(tree),staging,0,new long[]{0,0});
                // An incomplete/unsupported original mod is retained for inspection, never activated automatically.
                String id="mod-"+UUID.randomUUID().toString().substring(0,8);
                ensureManifest(staging,id);
                completed=new File(root(a),id);if(!staging.renameTo(completed))throw new IOException("Cannot finish mod import");
            }catch(Exception e){error=e.getMessage();remove(staging);}
            finally{importing=false;}
            final File folder=completed;final String failure=error;
            a.runOnUiThread(()->{if(a.isFinishing()||a.isDestroyed())return;
                if(failure!=null)new AlertDialog.Builder(a).setTitle("Mod import failed").setMessage(failure).setPositiveButton("Close",null).show();
                else inspect(a,folder);
            });
        },"ra2-mod-import").start();
    }
    static void importZip(Activity a,Uri archive) {
        if(importing)return;importing=true;
        cancelRequested=false;
        ProgressDialog progress=new ProgressDialog(a);progress.setTitle("Importing mod");progress.setMessage("Extracting and checking ZIP contents…");
        progress.setIndeterminate(true);progress.setCancelable(false);
        progress.setButton(ProgressDialog.BUTTON_NEGATIVE,"Cancel",(dialog,which)->cancelRequested=true);progress.show();
        new Thread(()->{
            File staging=new File(root(a),".import-"+UUID.randomUUID());File completed=null;String error=null;
            try {
                if(!staging.mkdir())throw new IOException("Cannot create mod import folder");
                File payload=new File(staging,"payload");if(!payload.mkdir())throw new IOException("Cannot prepare ZIP extraction");
                extractZip(a,archive,payload);
                File[] top=payload.listFiles();
                if(top!=null&&top.length==1&&top[0].isDirectory()) {
                    File wrapper=top[0];File[] children=wrapper.listFiles();
                    if(children!=null)for(File child:children)if(!child.renameTo(new File(payload,child.getName())))throw new IOException("Cannot unpack the ZIP's top-level folder");
                    wrapper.delete();
                }
                File[] children=payload.listFiles();if(children!=null)for(File child:children)
                    if(!child.renameTo(new File(staging,child.getName())))throw new IOException("Cannot finalize imported files");
                payload.delete();
                String id="mod-"+UUID.randomUUID().toString().substring(0,8);
                ensureManifest(staging,id);
                completed=new File(root(a),id);if(!staging.renameTo(completed))throw new IOException("Cannot finish mod import");
            }catch(Exception e){error=e.getMessage()==null?"Invalid or unsupported ZIP archive":e.getMessage();remove(staging);}
            finally{importing=false;}
            final File folder=completed;final String failure=error;
            a.runOnUiThread(()->{if(progress.isShowing())progress.dismiss();if(a.isFinishing()||a.isDestroyed())return;
                if(failure!=null)new AlertDialog.Builder(a).setTitle("Mod ZIP import failed").setMessage(failure).setPositiveButton("Close",null).show();
                else inspect(a,folder);
            });
        },"ra2-mod-zip-import").start();
    }
    private static void extractZip(Activity a,Uri archive,File target) throws IOException {
        ContentResolver resolver=a.getContentResolver();Set<String> names=new HashSet<>();long total=0;int entries=0;
        try(InputStream raw=resolver.openInputStream(archive)) {
            if(raw==null)throw new IOException("Cannot read the selected ZIP archive");
            try(ZipInputStream zip=new ZipInputStream(new BufferedInputStream(raw))) {
                ZipEntry entry;
                while((entry=zip.getNextEntry())!=null) {
                    if(cancelRequested||a.isFinishing()||a.isDestroyed())throw new IOException("Import cancelled");
                    if(++entries>16384)throw new IOException("ZIP contains too many entries (limit 16384)");
                    String rawName=entry.getName();if(rawName==null||rawName.isEmpty()||rawName.startsWith("/")||rawName.startsWith("\\")||rawName.matches("^[A-Za-z]:.*"))throw new IOException("ZIP contains an unsafe path");
                    String normalized=rawName.replace('\\','/');String[] parts=normalized.split("/");
                    if(parts.length>13)throw new IOException("ZIP folder structure is too deep");
                    StringBuilder rel=new StringBuilder();
                    for(String part:parts) {
                        if(part.isEmpty())continue;
                        if(part.equals(".")||part.equals("..")||part.indexOf(':')>=0||part.matches(".*[\\x00-\\x1f\\x7f].*"))throw new IOException("ZIP contains an unsafe path");
                        if(rel.length()>0)rel.append('/');rel.append(part.toLowerCase(Locale.ROOT));
                    }
                    if(rel.length()==0)throw new IOException("ZIP contains an invalid entry name");
                    String key=rel.toString();if(!names.add(key))throw new IOException("ZIP contains duplicate paths: "+key);
                    File out=new File(target,key);String base=target.getCanonicalPath()+File.separator;
                    if(!out.getCanonicalPath().startsWith(base))throw new IOException("ZIP path escapes the import folder");
                    if(entry.isDirectory()) {if(!out.isDirectory()&&!out.mkdirs())throw new IOException("Cannot create ZIP folder");zip.closeEntry();continue;}
                    File parent=out.getParentFile();if(parent==null||(!parent.isDirectory()&&!parent.mkdirs()))throw new IOException("Cannot create ZIP folder");
                    try(OutputStream stream=new BufferedOutputStream(new FileOutputStream(out))) {
                        byte[] buffer=new byte[65536];long size=0;int n;
                        while((n=zip.read(buffer))!=-1) {
                            if(cancelRequested||a.isFinishing()||a.isDestroyed())throw new IOException("Import cancelled");
                            size+=n;total+=n;
                            if(size>2L*1024*1024*1024||total>8L*1024*1024*1024)throw new IOException("Expanded ZIP exceeds safe import size limits");
                            stream.write(buffer,0,n);
                        }
                    }
                    zip.closeEntry();
                }
            }
        } catch(IllegalArgumentException e) {throw new IOException("Malformed or encrypted ZIP archive",e);}
        if(entries==0)throw new IOException("ZIP archive is empty");
    }
    private static void ensureManifest(File folder,String id) throws IOException {
        File manifest=new File(folder,"mod.ini");if(manifest.isFile())return;
        try(Writer out=new OutputStreamWriter(new FileOutputStream(manifest),StandardCharsets.UTF_8)) {
            out.write("[Mod]\nId="+id+"\nName=Imported mod\nRequires=base\n");
        }
    }
    private static void copyTree(Activity a,Uri tree,String document,File target,int depth,long[] count) throws IOException {
        if(depth>12)throw new IOException("Mod folder is too deeply nested");
        Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,document);
        Set<String> names=new HashSet<>();
        try(Cursor c=a.getContentResolver().query(children,new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,DocumentsContract.Document.COLUMN_MIME_TYPE},null,null,null)){
            if(c==null)throw new IOException("Cannot read selected folder");
            while(c.moveToNext()){
                if(a.isFinishing()||a.isDestroyed())throw new IOException("Import cancelled");
                if(++count[0]>16384)throw new IOException("Mod contains too many entries");
                String name=c.getString(1);
                if(name==null||name.equals(".")||name.equals("..")||name.contains("/")||name.contains("\\")||name.contains(":")||name.matches(".*[\\x00-\\x1f\\x7f].*"))throw new IOException("Invalid mod filename");
                name=name.toLowerCase(Locale.ROOT);if(!names.add(name))throw new IOException("Conflicting mod filename: "+name);
                File out=new File(target,name);
                if(DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))){
                    if(!out.mkdir())throw new IOException("Cannot create "+name);
                    copyTree(a,tree,c.getString(0),out,depth+1,count);continue;
                }
                Uri source=DocumentsContract.buildDocumentUriUsingTree(tree,c.getString(0));
                try(InputStream in=a.getContentResolver().openInputStream(source);OutputStream stream=new FileOutputStream(out)){
                    if(in==null)throw new IOException("Cannot read "+name);
                    byte[] buffer=new byte[65536];long size=0;int n;
                    while((n=in.read(buffer))!=-1){
                        if(a.isFinishing()||a.isDestroyed())throw new IOException("Import cancelled");
                        size+=n;count[1]+=n;
                        if(size>2L*1024*1024*1024||count[1]>8L*1024*1024*1024)throw new IOException("Mod exceeds import size limits");
                        stream.write(buffer,0,n);
                    }
                }
            }
        }
    }
    private static void remove(File file){File[] children=file.listFiles();if(children!=null)for(File c:children)remove(c);file.delete();}
}

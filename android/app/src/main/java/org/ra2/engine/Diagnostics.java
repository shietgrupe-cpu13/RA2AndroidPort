package org.ra2.engine;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.widget.Toast;
import android.widget.EditText;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Local, bounded diagnostics. Nothing is uploaded automatically. */
final class Diagnostics {
    static File dir(Context c) { File d=new File(c.getFilesDir(),"diagnostics");d.mkdirs();return d; }
    private static void text(File f,String s) throws IOException {
        try(FileOutputStream out=new FileOutputStream(f)){out.write(s.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}
    }
    static void begin(Context c) {
        final File d=dir(c);
        try {
            String version=c.getPackageManager().getPackageInfo(c.getPackageName(),0).versionName;
            text(new File(d,"device.txt"),"version="+version+"\nmodel="+Build.MANUFACTURER+" "+Build.MODEL+
                "\nversion_code="+c.getPackageManager().getPackageInfo(c.getPackageName(),0).versionCode+
                "\nandroid="+Build.VERSION.RELEASE+" sdk="+Build.VERSION.SDK_INT+"\nabis="+java.util.Arrays.toString(Build.SUPPORTED_ABIS)+
                "\nstarted_ms="+System.currentTimeMillis()+"\npid="+android.os.Process.myPid()+"\n");
            text(new File(d,"running.txt"),"Game started; clean shutdown not recorded yet.\n");
            if(Build.VERSION.SDK_INT>=30) {
                ActivityManager am=(ActivityManager)c.getSystemService(Context.ACTIVITY_SERVICE);
                byte[] summary=("RA2 "+version).getBytes(StandardCharsets.UTF_8);
                am.setProcessStateSummary(java.util.Arrays.copyOf(summary,Math.min(summary.length,128)));
            }
        } catch(Exception ignored) { }
        final Thread.UncaughtExceptionHandler old=Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread,error)-> {
            try {
                StringWriter sw=new StringWriter();error.printStackTrace(new PrintWriter(sw));
                text(new File(d,"java-crash.txt"),"thread="+thread.getName()+"\n"+sw.toString().substring(0,Math.min(sw.toString().length(),65536)));
            } catch(Exception ignored) { }
            if(old!=null)old.uncaughtException(thread,error);
            else {android.os.Process.killProcess(android.os.Process.myPid());System.exit(1);}
        });
    }
    static void finish(Context c) {
        new File(dir(c),"running.txt").delete();
    }
    static void collectExits(Context c) {
        if(Build.VERSION.SDK_INT<30){
            try{text(new File(dir(c),"process-exits.txt"),"Android exit history requires Android 11 / API 30.\n");}catch(IOException ignored){}
            return;
        }
        try {
            ActivityManager am=(ActivityManager)c.getSystemService(Context.ACTIVITY_SERVICE);
            List<ApplicationExitInfo> exits=am.getHistoricalProcessExitReasons(c.getPackageName(),0,8);
            StringBuilder result=new StringBuilder();int traces=0;
            for(ApplicationExitInfo e:exits) {
                result.append("timestamp_ms=").append(e.getTimestamp()).append(" process=").append(e.getProcessName())
                    .append(" reason=").append(e.getReason()).append(" status=").append(e.getStatus())
                    .append(" importance=").append(e.getImportance()).append(" pss_kb=").append(e.getPss())
                    .append(" rss_kb=").append(e.getRss()).append(" description=").append(e.getDescription()).append('\n');
                byte[] state=e.getProcessStateSummary();if(state!=null)result.append("state_summary=").append(new String(state,StandardCharsets.UTF_8)).append('\n');
                if(traces<2 && (e.getReason()==ApplicationExitInfo.REASON_CRASH_NATIVE || e.getReason()==ApplicationExitInfo.REASON_ANR)) {
                    File trace=new File(dir(c),"exit-"+e.getTimestamp()+".trace");
                    if(!trace.exists())try(InputStream in=e.getTraceInputStream()) {
                        if(in!=null)try(FileOutputStream out=new FileOutputStream(trace)){copy(in,out,2*1024*1024);}
                    }
                    ++traces;
                }
            }
            text(new File(dir(c),"process-exits.txt"),result.toString());
            File[] files=dir(c).listFiles((d,name)->name.startsWith("exit-")&&name.endsWith(".trace"));
            if(files!=null){java.util.Arrays.sort(files,(a,b)->Long.compare(b.lastModified(),a.lastModified()));for(int i=2;i<files.length;++i)files[i].delete();}
        } catch(Exception error) {
            try{text(new File(dir(c),"process-exits.txt"),"Exit history unavailable: "+error.getClass().getSimpleName()+"\n");}catch(IOException ignored){}
        }
    }
    private static void copy(InputStream in,OutputStream out,int limit)throws IOException {
        byte[] buf=new byte[8192];int n,total=0;
        while(total<limit && (n=in.read(buf,0,Math.min(buf.length,limit-total)))>0){out.write(buf,0,n);total+=n;}
    }
    static void share(Activity a) {
        final EditText description=new EditText(a);
        description.setHint("Optional: what happened, which unit, and what did you expect? Logs are included automatically.");
        description.setMinLines(3);
        description.setMaxLines(6);
        description.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(2000)});
        new AlertDialog.Builder(a).setTitle("Share game report").setView(description)
            .setNegativeButton("Cancel",null)
            .setPositiveButton("Share",(dialog,which)->shareReport(a,description.getText().toString())).show();
    }
    private static void shareReport(Activity a,String description) {
        Toast.makeText(a,"Preparing game report…",Toast.LENGTH_SHORT).show();
        new Thread(()-> {
            try {
                collectExits(a);
                String version=a.getPackageManager().getPackageInfo(a.getPackageName(),0).versionName;
                ActivityManager.MemoryInfo memory=new ActivityManager.MemoryInfo();
                ((ActivityManager)a.getSystemService(Context.ACTIVITY_SERVICE)).getMemoryInfo(memory);
                java.text.SimpleDateFormat utc=new java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'",java.util.Locale.ROOT);
                utc.setTimeZone(java.util.TimeZone.getTimeZone("UTC"));
                final String entered=description.trim();
                text(new File(dir(a),"report.txt"),"schema=3\ncreated_utc="+
                    utc.format(new java.util.Date())+
                    "\nversion="+version+"\nversion_code="+a.getPackageManager().getPackageInfo(a.getPackageName(),0).versionCode+
                    "\npackage="+a.getPackageName()+"\navailable_memory_bytes="+memory.availMem+
                    "\nlow_memory="+memory.lowMemory+
                    "\ndescription_provided="+!entered.isEmpty()+
                    "\nuser_description="+(entered.isEmpty()?"Not provided; see engine logs and skirmish.txt for automatic session context.":entered)+"\n");
                File reports=new File(a.getCacheDir(),"reports");reports.mkdirs();
                File[] old=reports.listFiles((d,n)->n.matches("ra2-report-[0-9]+\\.zip"));
                if(old!=null){java.util.Arrays.sort(old,(a1,b)->Long.compare(b.lastModified(),a1.lastModified()));for(int i=2;i<old.length;++i)old[i].delete();}
                File zip=new File(reports,"ra2-report-"+System.currentTimeMillis()+".zip");
                try(ZipOutputStream out=new ZipOutputStream(new FileOutputStream(zip))) {
                    out.putNextEntry(new ZipEntry("README.txt"));
                    out.write(("RA2 local diagnostics. Includes recent actions, resolved asset names, skirmish settings and Android exit info when available.\n"+
                        "report.txt contains the description and report time. CHECK lines are nonfatal observations, not proof of a gameplay bug. UNIT/STRUCTURE samples are bounded.\n"+
                        "PERFORMANCE records CPU stage durations and call rates, not GPU time or phone temperature.\n"+
                        "Native .trace files can be Android tombstone protobufs, not plain text. Their timestamps may predate this session.\n"+
                        "No retail archives or saves are copied. Text logs may contain local file paths or match details. Logs/traces are capped in size and can be incomplete.\n").getBytes(StandardCharsets.UTF_8));out.closeEntry();
                    File[] files=dir(a).listFiles();if(files!=null)for(File f:files) {
                        if(!f.isFile() || !allowed(f.getName()))continue;
                        out.putNextEntry(new ZipEntry(f.getName()));try(FileInputStream in=new FileInputStream(f)){copy(in,out,2*1024*1024);}out.closeEntry();
                    }
                }
                a.runOnUiThread(()-> {
                    Uri uri=Uri.parse("content://"+a.getPackageName()+".reports/"+zip.getName());
                    Intent send=new Intent(Intent.ACTION_SEND).setType("application/zip").putExtra(Intent.EXTRA_STREAM,uri)
                        .putExtra(Intent.EXTRA_SUBJECT,"RA2 game diagnostics "+version).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    send.setClipData(ClipData.newRawUri("RA2 game report",uri));
                    a.startActivity(Intent.createChooser(send,"Share game report"));
                });
            }catch(Exception ex){a.runOnUiThread(()->Toast.makeText(a,"Could not prepare report: "+ex.getClass().getSimpleName(),Toast.LENGTH_LONG).show());}
        },"RA2-report").start();
    }
    private static boolean allowed(String name) {
        return name.matches("(?:engine|assets)-(?:current|previous)\\.log") || name.equals("device.txt") || name.equals("skirmish.txt") ||
            name.equals("process-exits.txt") || name.equals("java-crash.txt") || name.equals("running.txt") || name.equals("report.txt") || name.equals("mod-inspection.txt") || name.matches("exit-[0-9]+\\.trace");
    }
}

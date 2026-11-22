package site.multitap.piccoletto;

import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

import java.util.ArrayList;
import java.util.Arrays;

import org.libsdl.app.SDLActivity;

// piccoletto's activity: SDL's activity with SDL3 linked statically into
// libmain.so, so only "main" is loaded. Everything else (files, orientation,
// full screen) is done from the native side through SDL, except the APK's
// install time, which only Java can read.
public class PiccolettoActivity extends SDLActivity {
    private static final String TAG = "piccoletto";

    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);   // loads libmain.so: nativeSetenv works from here on
        if (mBrokenLibraries) {
            return;
        }
        // When this APK was installed: the shader presets it ships are copied
        // out of it again only when this changes (frontends-common's app_assets).
        try {
            long installed = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
            nativeSetenv("MULTITAP_APK_STAMP", Long.toString(installed));
        } catch (PackageManager.NameNotFoundException e) {
            Log.w(TAG, "can't read the package's update time: shader presets copied at every start");
        }
    }

    // The desktop's testing flags (--screen gamepad, --aspect 2:3, ...) from
    // the launch intent, to look at a screen without tapping through:
    //   adb shell am start -S -n site.multitap.piccoletto/.PiccolettoActivity --es args "--screen gamepad"
    // Opened from piccoletto://pair: --pair-link (a link naming a piccolo and
    // its beacon; not used for pairing since 2026-11-22, kept for the links to
    // come: main.cpp's open_link).
    @Override
    protected String[] getArguments() {
        ArrayList<String> list = new ArrayList<String>();
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args != null && !args.trim().isEmpty()) {
            list.addAll(Arrays.asList(args.trim().split("\\s+")));
        }
        String link = pairLink(getIntent());
        if (link != null) {
            list.add("--pair-link");
            list.add(link);
        }
        return list.toArray(new String[0]);
    }

    // Already running (singleInstance): the link reaches the app as dropped
    // text (SDL_EVENT_DROP_FILE; main.cpp's open_link).
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        String link = pairLink(intent);
        if (link != null) {
            SDLActivity.onNativeDropFile(link);
        }
    }

    // SEND CODE (PAIR across the internet): Android's share sheet with the
    // code, to get it to whoever sits at the piccolo. Called from main.cpp
    // through JNI on SDL's thread; the chooser opens on the UI thread.
    public void shareText(final String text) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent send = new Intent(Intent.ACTION_SEND);
                send.setType("text/plain");
                send.putExtra(Intent.EXTRA_TEXT, text);
                startActivity(Intent.createChooser(send, null));
            }
        });
    }

    // piccoletto://pair?k=..&b=.. as the pair link the app reads
    // (https://get-piccolo.app/pair/#k=..&b=..); null if it isn't one.
    private static String pairLink(Intent intent) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction())) {
            return null;
        }
        Uri data = intent.getData();
        if (data == null || !"piccoletto".equals(data.getScheme()) || !"pair".equals(data.getHost())) {
            return null;
        }
        String fields = data.getEncodedQuery();
        if (fields == null || fields.isEmpty()) {
            return null;
        }
        return "https://get-piccolo.app/pair/#" + fields;
    }
}

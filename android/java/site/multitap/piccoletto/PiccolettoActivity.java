package site.multitap.piccoletto;

import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;

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
    @Override
    protected String[] getArguments() {
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty()) {
            return new String[0];
        }
        return args.trim().split("\\s+");
    }
}

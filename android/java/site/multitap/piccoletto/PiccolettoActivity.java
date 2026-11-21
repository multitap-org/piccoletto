package site.multitap.piccoletto;

import android.content.Intent;
import android.net.Uri;

import java.util.ArrayList;
import java.util.Arrays;

import org.libsdl.app.SDLActivity;

// piccoletto's activity: SDL's activity with SDL3 linked statically into
// libmain.so, so only "main" is loaded. Everything else (files, orientation,
// full screen) is done from the native side through SDL.
public class PiccolettoActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    // The desktop's testing flags (--screen gamepad, --aspect 2:3, ...) from
    // the launch intent, to look at a screen without tapping through:
    //   adb shell am start -S -n site.multitap.piccoletto/.PiccolettoActivity --es args "--screen gamepad"
    // Opened from piccoletto://pair (the site's /pair page): --pair-link.
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
    // text (SDL_EVENT_DROP_FILE; main.cpp opens PAIR with it).
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        String link = pairLink(intent);
        if (link != null) {
            SDLActivity.onNativeDropFile(link);
        }
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

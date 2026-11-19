package org.multitap.piccoletto;

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
    //   adb shell am start -S -n org.multitap.piccoletto/.PiccolettoActivity --es args "--screen gamepad"
    @Override
    protected String[] getArguments() {
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty()) {
            return new String[0];
        }
        return args.trim().split("\\s+");
    }
}

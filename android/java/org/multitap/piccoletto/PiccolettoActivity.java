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
}

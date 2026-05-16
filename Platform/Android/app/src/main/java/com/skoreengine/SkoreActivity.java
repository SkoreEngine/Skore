package com.skoreengine;

import org.libsdl.app.SDLActivity;

public class SkoreActivity extends SDLActivity {

    @Override
    protected String getMainFunction() {
        return "SkoreMain";
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
                "SDL3",
                "SkoreRuntime",
                "SkorePlayer"
        };
    }
}

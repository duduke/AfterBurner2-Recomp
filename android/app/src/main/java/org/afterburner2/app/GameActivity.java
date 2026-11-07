package org.afterburner2.app;

import org.libsdl.app.SDLActivity;

/** SDL owns surfaces, controllers, the document picker and Android lifecycle. */
public final class GameActivity extends SDLActivity {
    @Override protected String[] getArguments() {
        // ADB-only bounded diagnostics for development APKs. Release builds
        // always launch the normal frontend and never accept benchmark inputs.
        if ((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
            if (getIntent().getBooleanExtra("ab2.profile", false)) {
                SDLActivity.nativeSetenv("AB2_FRAME_TIMINGS", getFilesDir() + "/frame-timings.csv");
            }
            String[] arguments = getIntent().getStringArrayExtra("ab2.arguments");
            if (arguments != null) return arguments;
        }
        return super.getArguments();
    }

    // content:// URI paths are provider identifiers, not user filenames.
    public byte[] getImportDisplayName(String source) {
        android.net.Uri uri = android.net.Uri.parse(source);
        try (android.database.Cursor cursor = getContentResolver().query(uri,
                new String[] { android.provider.OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0).getBytes(java.nio.charset.StandardCharsets.UTF_8);
        } catch (RuntimeException ignored) { }
        return new byte[0];
    }

    @Override protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }
}

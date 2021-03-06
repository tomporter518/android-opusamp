package net.opusapp.player.core.service.providers.local.scanner;

import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.preference.PreferenceManager;

import net.opusapp.player.R;
import net.opusapp.player.core.service.providers.MediaManager;
import net.opusapp.player.core.service.providers.local.LocalMediaManager;
import net.opusapp.player.core.service.providers.local.database.Entities;
import net.opusapp.player.core.service.providers.local.database.OpenHelper;
import net.opusapp.player.core.service.utils.CursorUtils;
import net.opusapp.player.ui.utils.PlayerApplication;
import net.opusapp.player.utils.backport.android.content.SharedPreferencesCompat;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

public class MediaScanner {

    public static final String TAG = MediaScanner.class.getSimpleName();



    private ScannerThread mScannerThread;

    private LocalMediaManager mManager;

    public MediaScanner(final LocalMediaManager parent) {
        mScannerThread = null;
        mManager = parent;
    }

    public static Set<String> getCoverExtensions() {
        final SharedPreferences sharedPrefs = PreferenceManager.getDefaultSharedPreferences(PlayerApplication.context);

        final Set<String> defaults = new HashSet<>(Arrays.asList(PlayerApplication.context.getResources().getStringArray(R.array.cover_exts)));
        final Set<String> extensionSet = SharedPreferencesCompat.getStringSet(sharedPrefs, PlayerApplication.context.getString(R.string.key_cover_exts), defaults);

        if(extensionSet.size() == 0) {
            return defaults;
        }

        return extensionSet;
    }

    public static Set<String> getMediaExtensions(int providerId) {
        final Set<String> audioFilesExtensions = new HashSet<>();

        final SQLiteDatabase database = OpenHelper.getInstance(providerId).getWritableDatabase();

        final String[] columns = new String[] {
                Entities.FileExtensions.COLUMN_FIELD_EXTENSION
        };

        final int COLUMN_EXTENSION = 0;
        final Cursor cursor = database.query(Entities.FileExtensions.TABLE_NAME, columns, null, null, null, null, null, null);

        if (CursorUtils.ifNotEmpty(cursor)) {
            while (cursor.moveToNext()) {
                audioFilesExtensions.add(cursor.getString(COLUMN_EXTENSION).toLowerCase());
            }
            CursorUtils.free(cursor);
        }

        return audioFilesExtensions;
    }


    public synchronized void start() {
        if (mScannerThread == null) {
            mScannerThread = new ScannerThread(this);
            mScannerThread.start();
        }
    }

    public synchronized void stop() {
        if (mScannerThread != null) {
            mScannerThread.requestCancellation();
        }
    }

    public MediaManager getManager() {
        return mManager;
    }

    public synchronized boolean isRunning() {
        return mScannerThread != null && mScannerThread.isRunning();
    }

    public synchronized void scannerHasTerminated() {
        mScannerThread = null;
    }
}

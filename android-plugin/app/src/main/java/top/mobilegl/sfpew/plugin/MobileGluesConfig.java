// SimpleFPEWrapper - android-plugin/app/src/main/java/top/mobilegl/sfpew/plugin/MobileGluesConfig.java
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

package top.mobilegl.sfpew.plugin;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * The MobileGlues settings in MG/config.json, as the MobileGlues library reads
 * them (MobileGlues-cpp/config/settings.cpp): integers under fixed keys.
 *
 * The file belongs to MobileGlues, and other tools (the MobileGlues plugin)
 * write keys this screen does not know - the multi-draw orders, among others.
 * So a save rewrites only the keys edited here, on top of the object as it was
 * read, and a file that cannot be parsed is never overwritten.
 */
final class MobileGluesConfig {
    /** MobileGlues' default directory (MG_DIR_PATH overrides it in the library). */
    static final File DIRECTORY = new File("/sdcard/MG");
    static final File FILE = new File(DIRECTORY, "config.json");

    /** customGLVersion values the library uses as given; anything else it clamps. */
    static final int[] GL_VERSIONS = {0, 46, 45, 44, 43, 42, 41, 40, 33, 32};

    private final JSONObject json;

    int angle;
    int noError;
    int depthClearFix;
    int glVersion;
    int glslCacheMebibytes;
    boolean extComputeShader;
    boolean extTimerQuery;
    boolean extDirectStateAccess;
    int fsr1;

    private MobileGluesConfig(JSONObject json) {
        this.json = json;
        // The library's own defaults (settings.cpp) for keys the file lacks.
        angle = json.optInt("enableANGLE", 1);
        noError = json.optInt("enableNoError", 0);
        depthClearFix = json.optInt("angleDepthClearFixMode", 0);
        glVersion = clampGlVersion(json.optInt("customGLVersion", 0));
        final int cache = json.optInt("maxGlslCacheSize", 32);
        glslCacheMebibytes = Math.max(cache, 0); // <= 0 is "off" to the library
        extComputeShader = json.optInt("enableExtComputeShader", 0) != 0;
        extTimerQuery = json.optInt("enableExtTimerQuery", 1) != 0;
        extDirectStateAccess = json.optInt("enableExtDirectStateAccess", 0) != 0;
        fsr1 = json.optInt("fsr1Setting", 0);
    }

    /**
     * The settings in {@link #FILE}, or the defaults when there is none yet.
     *
     * @throws IOException when the file exists but cannot be read or parsed;
     *                     then nothing must be written over it.
     */
    static MobileGluesConfig load() throws IOException {
        if (!FILE.isFile()) return new MobileGluesConfig(new JSONObject());
        final String text = new String(Files.readAllBytes(FILE.toPath()), StandardCharsets.UTF_8);
        try {
            return new MobileGluesConfig(text.trim().isEmpty() ? new JSONObject() : new JSONObject(text));
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
    }

    /** Writes the edited keys back, keeping every other key as it was read. */
    void save() throws IOException {
        try {
            json.put("enableANGLE", angle);
            json.put("enableNoError", noError);
            json.put("angleDepthClearFixMode", depthClearFix);
            json.put("customGLVersion", glVersion);
            // The library treats any value <= 0 as "no cache"; -1 is what it
            // has always been written as.
            json.put("maxGlslCacheSize", glslCacheMebibytes > 0 ? glslCacheMebibytes : -1);
            json.put("enableExtComputeShader", extComputeShader ? 1 : 0);
            json.put("enableExtTimerQuery", extTimerQuery ? 1 : 0);
            json.put("enableExtDirectStateAccess", extDirectStateAccess ? 1 : 0);
            json.put("fsr1Setting", fsr1);
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
        if (!DIRECTORY.isDirectory() && !DIRECTORY.mkdirs()) throw new IOException("cannot create " + DIRECTORY);
        // Write a sibling, then rename over the file: a crash half way never
        // leaves MobileGlues a truncated config.
        final File partial = new File(DIRECTORY, FILE.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(partial)) {
            out.write(json.toString(4).getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
        if (!partial.renameTo(FILE)) throw new IOException("cannot replace " + FILE);
    }

    /**
     * The library clamps a customGLVersion it does not list rather than
     * resetting it (47 is 4.6, 34..39 are 3.3, 1..31 are 3.2); reading such a
     * value back as "default" would rewrite it to 0 - 4.0 to the library - on
     * the next save without the user touching it.
     */
    static int clampGlVersion(int wire) {
        for (int listed : GL_VERSIONS) {
            if (listed == wire) return wire;
        }
        if (wire > 46) return 46;
        if (wire >= 34 && wire <= 39) return 33;
        if (wire >= 1 && wire <= 31) return 32;
        return 0;
    }
}

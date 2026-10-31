// SimpleFPEWrapper - android-plugin/app/src/main/java/top/mobilegl/sfpew/plugin/MainActivity.java
// Copyright (c) 2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

package top.mobilegl.sfpew.plugin;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.text.InputType;
import android.util.TypedValue;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * The plugin's own screen: what it is, which backends it carries, and the
 * MobileGlues settings. SFPEW's and MobileGL's options are the launcher's to
 * show (the renderer config's env), but MobileGlues reads its settings from a
 * file, so they are edited here - the same file the MobileGlues plugin edits.
 */
public final class MainActivity extends Activity {
    private static final int REQUEST_STORAGE = 1;

    private LinearLayout settings;
    private TextView status;
    private MobileGluesConfig config;

    private Spinner angle;
    private Spinner noError;
    private Spinner depthClearFix;
    private Spinner glVersion;
    private EditText glslCache;
    private Switch extComputeShader;
    private Switch extTimerQuery;
    private Switch extDirectStateAccess;
    private Switch fsr1;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        final int pad = dp(16);
        root.setPadding(pad, pad, pad, pad);

        heading(root, getString(R.string.about_title));
        text(root, getString(R.string.about_body, MobileGluesConfig.FILE.getPath()));
        text(root, getString(R.string.bundled_backends, bundledBackends()));
        text(root, "SFPEW " + BuildConfig.VERSION_NAME);

        heading(root, getString(R.string.mobileglues_title));
        settings = new LinearLayout(this);
        settings.setOrientation(LinearLayout.VERTICAL);
        root.addView(settings);
        status = text(root, "");

        final ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Storage access may have just been granted in the system settings.
        showMobileGluesSettings();
    }

    private void showMobileGluesSettings() {
        settings.removeAllViews();
        if (!BuildConfig.HAS_MOBILEGLUES) {
            text(settings, getString(R.string.mobileglues_absent));
            return;
        }
        if (!hasStorageAccess()) {
            text(settings, getString(R.string.storage_needed, MobileGluesConfig.FILE.getPath()));
            final Button grant = new Button(this);
            grant.setText(R.string.grant_storage);
            grant.setOnClickListener(v -> requestStorageAccess());
            settings.addView(grant);
            return;
        }
        try {
            config = MobileGluesConfig.load();
        } catch (IOException e) {
            config = null;
            text(settings, getString(R.string.load_failed, MobileGluesConfig.FILE.getPath(), e.getMessage()));
            return;
        }

        angle = spinner(R.string.angle, getResources().getStringArray(R.array.angle_options), config.angle);
        noError = spinner(R.string.no_error, getResources().getStringArray(R.array.no_error_options), config.noError);
        depthClearFix = spinner(R.string.depth_clear_fix, getResources().getStringArray(R.array.depth_clear_fix_options),
                                config.depthClearFix);
        final String[] versions = new String[MobileGluesConfig.GL_VERSIONS.length];
        int versionIndex = 0;
        for (int i = 0; i < versions.length; ++i) {
            final int wire = MobileGluesConfig.GL_VERSIONS[i];
            versions[i] = wire == 0 ? getString(R.string.gl_version_default) : "OpenGL " + wire / 10 + "." + wire % 10;
            if (wire == config.glVersion) versionIndex = i;
        }
        glVersion = spinner(R.string.gl_version, versions, versionIndex);

        text(settings, getString(R.string.glsl_cache));
        glslCache = new EditText(this);
        glslCache.setInputType(InputType.TYPE_CLASS_NUMBER);
        glslCache.setText(String.valueOf(config.glslCacheMebibytes));
        settings.addView(glslCache);

        extComputeShader = toggle(R.string.ext_compute_shader, config.extComputeShader);
        extTimerQuery = toggle(R.string.ext_timer_query, config.extTimerQuery);
        extDirectStateAccess = toggle(R.string.ext_dsa, config.extDirectStateAccess);
        fsr1 = toggle(R.string.fsr1, config.fsr1 != 0);

        final Button save = new Button(this);
        save.setText(R.string.save);
        save.setOnClickListener(this::save);
        settings.addView(save);
    }

    private void save(View unused) {
        if (config == null) return;
        config.angle = angle.getSelectedItemPosition();
        config.noError = noError.getSelectedItemPosition();
        config.depthClearFix = depthClearFix.getSelectedItemPosition();
        config.glVersion = MobileGluesConfig.GL_VERSIONS[glVersion.getSelectedItemPosition()];
        try {
            config.glslCacheMebibytes = Integer.parseInt(glslCache.getText().toString().trim());
        } catch (NumberFormatException e) {
            config.glslCacheMebibytes = 0;
        }
        config.extComputeShader = extComputeShader.isChecked();
        config.extTimerQuery = extTimerQuery.isChecked();
        config.extDirectStateAccess = extDirectStateAccess.isChecked();
        // The switch is on/off, the library has quality presets: an existing
        // preset survives a save that left the switch on.
        if (!fsr1.isChecked()) config.fsr1 = 0;
        else if (config.fsr1 == 0) config.fsr1 = 1;
        try {
            config.save();
            status.setText(getString(R.string.saved, MobileGluesConfig.FILE.getPath()));
        } catch (IOException e) {
            status.setText(getString(R.string.save_failed, e.getMessage()));
        }
    }

    private String bundledBackends() {
        final List<String> names = new ArrayList<>();
        names.add(getString(R.string.backend_system));
        if (BuildConfig.HAS_MOBILEGLUES) names.add("MobileGlues (libmobileglues.so)");
        if (BuildConfig.HAS_MOBILEGL) names.add("MobileGL (libMobileGL.so)");
        return String.join(", ", names);
    }

    private boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                             Uri.parse("package:" + getPackageName()));
            try {
                startActivity(intent);
            } catch (RuntimeException e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            requestPermissions(new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_STORAGE);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE) showMobileGluesSettings();
    }

    private Spinner spinner(int label, String[] options, int selected) {
        text(settings, getString(label));
        final Spinner spinner = new Spinner(this);
        spinner.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, options));
        spinner.setSelection(selected >= 0 && selected < options.length ? selected : 0);
        settings.addView(spinner);
        return spinner;
    }

    private Switch toggle(int label, boolean checked) {
        final Switch toggle = new Switch(this);
        toggle.setText(label);
        toggle.setChecked(checked);
        toggle.setPadding(0, dp(8), 0, dp(8));
        settings.addView(toggle);
        return toggle;
    }

    private TextView heading(LinearLayout parent, String value) {
        final TextView view = text(parent, value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        view.setPadding(0, dp(16), 0, dp(8));
        return view;
    }

    private TextView text(LinearLayout parent, String value) {
        final TextView view = new TextView(this);
        view.setText(value);
        view.setPadding(0, dp(4), 0, dp(4));
        parent.addView(view);
        return view;
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics());
    }
}

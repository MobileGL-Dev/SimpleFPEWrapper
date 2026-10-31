# SFPEW renderer plugin

An Android renderer plugin for Minecraft launchers (FCL, Zalith and others
that read `fclPlugin_V2`), carrying SimpleFPEWrapper and the backends it can
run on:

| backend | library | source |
|---|---|---|
| system GLES/EGL | `libEGL.so` (the device's) | - |
| MobileGlues | `libmobileglues.so` | [MobileGL-Dev/MobileGlues](https://github.com/MobileGL-Dev/MobileGlues), `main` |
| MobileGL | `libMobileGL.so` | [MobileGL-Dev/MobileGL](https://github.com/MobileGL-Dev/MobileGL), `dev` |

The launcher loads `libSimpleFPEWrapper.so` for both GL and EGL. The wrapper
knows no backend by name: it loads whatever library `SFPEW_EGL` names - a bare
name is looked for beside the wrapper first, which is where the APK puts the
backends - and the plugin offers the bundled ones as that option's choices.

## Options

- **SFPEW** - `SFPEW_EGL` (the backend) and the wrapper's switches, set in the
  launcher: the renderer config in `app/build.gradle.kts`.
- **MobileGL** - its `MOBILEGL_*` options, also in the launcher, titled
  "MobileGL: ..." since they act only with that backend.
- **MobileGlues** - reads `/sdcard/MG/config.json`; the plugin's own screen
  edits it (keeping every key it does not show), as the MobileGlues plugin does.

## Building

The backends are built from source, from checkouts at `backends/MobileGlues`
and `backends/MobileGL` (or `-Psfpew.mobilegluesDir=` / `-Psfpew.mobileglDir=`).
A backend whose checkout is absent is left out of the APK and of the choices.

```sh
git clone --recurse-submodules -b main https://github.com/MobileGL-Dev/MobileGlues.git backends/MobileGlues
git clone --recurse-submodules -b dev https://github.com/MobileGL-Dev/MobileGL.git backends/MobileGL
(cd backends/MobileGL/3rdparty/glslang && python3 update_glslang_sources.py)
./gradlew :app:assembleRelease          # JDK 17+; arm64-v8a, or -Psfpew.abis=...
```

The APK is signed with `keystore.jks` and `SIGNING_*` from the environment
when present, with the debug key otherwise. `.github/workflows/apk.yml` builds
it the same way on every push.

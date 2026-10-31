import com.android.build.gradle.internal.api.ApkVariantOutputImpl
import com.launchers_plugin.renderer.buildscript.RendererConfig
import com.launchers_plugin.renderer.buildscript.buildEnvs
import com.launchers_plugin.renderer.buildscript.buildJsonValue
import com.launchers_plugin.renderer.buildscript.legacyManifest
import com.launchers_plugin.renderer.buildscript.nativePath
import com.launchers_plugin.renderer.buildscript.renderer

buildscript {
    repositories {
        maven("https://jitpack.io")
    }
    dependencies {
        classpath("com.github.ZalithLauncher.RendererPlugin-v2:dsl:1.0-alpha6")
    }
}

plugins {
    id("com.android.application")
}

apply(plugin = "com.launchers_plugin.renderer.dsl")

// The backends this build carries (settings.gradle.kts includes a backend's
// module only when its source checkout is there). The names of backends live
// here and nowhere in the wrapper: the wrapper only ever loads whatever
// library SFPEW_EGL names.
val hasMobileGlues = rootProject.findProject(":MobileGlues") != null
val hasMobileGl = rootProject.findProject(":MobileGL") != null

// SFPEW_EGL's choices: the system's GLES/EGL by default, then each bundled
// backend's library. A bare name is looked up beside the wrapper first, which
// is where the APK puts the backends.
val systemBackend = "libEGL.so"
val bundledBackends = buildList {
    if (hasMobileGlues) add("libmobileglues.so")
    if (hasMobileGl) add("libMobileGL.so")
}

fun Project.runGit(vararg arguments: String): String? = runCatching {
    ProcessBuilder("git", *arguments)
        .directory(rootDir)
        .redirectErrorStream(true)
        .start()
        .let { process ->
            val output = process.inputStream.bufferedReader().use { it.readText().trim() }
            output.takeIf { process.waitFor() == 0 && it.isNotEmpty() }
        }
}.getOrNull()

// The wrapper's calendar version (SimpleFPEWrapper/version.h), with the
// commit; the code counts this month's commits so every build upgrades the
// last.
val versionHeader = rootDir.resolve("../SimpleFPEWrapper/version.h").readText()
val versionMatch = Regex("""kSfpewVersion = \{(\d+), (\d+), (\d+),""").find(versionHeader)
    ?: error("no kSfpewVersion in SimpleFPEWrapper/version.h")
val sfpewMajor = versionMatch.groupValues[1].toInt()
val sfpewMinor = versionMatch.groupValues[2].toInt()
val gitShortHash = runGit("rev-parse", "--short=7", "HEAD") ?: "nogit"
val monthlyRevision = runGit(
    "rev-list", "--count",
    "--since=${String.format("%d-%02d-01T00:00:00", 2000 + sfpewMajor, sfpewMinor)}",
    "HEAD",
)?.toIntOrNull() ?: 0
val apkSuffix = (findProperty("sfpew.apkSuffix") ?: System.getenv("SFPEW_APK_SUFFIX") ?: gitShortHash)
    .toString().ifBlank { "nogit" }

val signingStoreFile = file("../keystore.jks")
val signingStorePassword = System.getenv("SIGNING_STORE_PASSWORD")
val signingKeyAlias = System.getenv("SIGNING_KEY_ALIAS")
val signingKeyPassword = System.getenv("SIGNING_KEY_PASSWORD")
val releaseSigningReady = signingStoreFile.exists() &&
    !signingStorePassword.isNullOrEmpty() && !signingKeyAlias.isNullOrEmpty() && !signingKeyPassword.isNullOrEmpty()

val pluginRendererConfig = buildJsonValue {
    renderer(
        displayName = "SFPEW",
        rendererId = "opengles3",
        rendererGLPath = nativePath("libSimpleFPEWrapper.so"),
        // The wrapper exports the whole EGL API too, forwarding to the backend
        // SFPEW_EGL picked, so one path serves both and the choice stays a
        // single option.
        rendererEGLPath = nativePath("libSimpleFPEWrapper.so"),
        dlopenLibPaths = emptyList(),
        env = buildEnvs {
            normal("LIBGL_ES", "3")

            // SimpleFPEWrapper
            if (bundledBackends.isNotEmpty()) {
                selectable(
                    key = "SFPEW_EGL",
                    title = RendererConfig.MetaString("sfpew_backend_title"),
                    items = RendererConfig.EnvItems(systemBackend, bundledBackends),
                )
            }
            toggleable("SFPEW_RELAXED_CONTEXT", "1", false, RendererConfig.MetaString("sfpew_relaxed_context_title"))
            toggleable("SFPEW_MANUAL_MIPGEN", "0", false, RendererConfig.MetaString("sfpew_no_mipmap_repair_title"))
            toggleable("SFPEW_NO_MULTIDRAWARRAYS", "1", false, RendererConfig.MetaString("sfpew_no_multidrawarrays_title"))
            toggleable("SFPEW_NO_LIST_BATCH", "1", false, RendererConfig.MetaString("sfpew_no_list_batch_title"))
            toggleable("SFPEW_NO_LIST_ARENA", "1", false, RendererConfig.MetaString("sfpew_no_list_arena_title"))
            customizable("SFPEW_DUMP_SHADERS", null, RendererConfig.MetaString("sfpew_dump_shaders_title"))

            // MobileGL: read by libMobileGL.so, so only meaningful with that
            // backend - their titles say so.
            if (hasMobileGl) {
                selectable(
                    key = "MOBILEGL_BACKEND_TYPE",
                    title = RendererConfig.MetaString("mobilegl_backend_type_title"),
                    items = RendererConfig.EnvItems("DirectGLES", listOf("DirectVulkan")),
                )
                toggleable("MOBILEGL_DISABLE_TIMERQUERY", "1", false, RendererConfig.MetaString("mobilegl_disable_timerquery_title"))
                selectable(
                    key = "MOBILEGL_SUBGROUP_EMULATION",
                    title = RendererConfig.MetaString("mobilegl_subgroup_emulation_title"),
                    items = RendererConfig.EnvItems("Auto", listOf("ForceOn", "ForceOff")),
                )
                toggleable("MOBILEGL_MAGMA_R11G11B10F_FALLBACK", "1", false, RendererConfig.MetaString("mobilegl_magma_r11g11b10f_fallback_title"))
                customizable("MOBILEGL_FRAMES_IN_FLIGHT", "3", RendererConfig.MetaString("mobilegl_frames_inflight_title"))
                toggleable("MOBILEGL_ESPRYT_AVOID_SAMPLER_MIPMAP_MIN_FILTER", "1", false, RendererConfig.MetaString("mobilegl_avoid_sampler_mipmap_min_filter_title"))
                toggleable("MOBILEGL_COHERENT_AS_FLUSH", "1", false, RendererConfig.MetaString("mobilegl_coherent_as_flush_title"))
                toggleable("MOBILEGL_RELAXED_SEMANTICS", "1", false, RendererConfig.MetaString("mobilegl_relaxed_semantics_title"))
                toggleable("MOBILEGL_ESPRYT_USE_ANGLE", "1", false, RendererConfig.MetaString("mobilegl_use_angle_title"))
            }
            // MobileGlues takes its settings from MG/config.json, which the
            // plugin's own screen edits (MainActivity), not from the env.
        },
        minMCVer = null,
        maxMCVer = null,
    )
}

android {
    namespace = "top.mobilegl.sfpew.plugin"
    compileSdk = 36

    defaultConfig {
        applicationId = "top.mobilegl.sfpew.plugin"
        minSdk = 26
        targetSdk = 34
        versionCode = sfpewMajor * 1_000_000 + sfpewMinor * 10_000 + monthlyRevision
        versionName = "%d.%02d.%s".format(sfpewMajor, sfpewMinor, gitShortHash)
        resValue("string", "config", pluginRendererConfig)
        buildConfigField("boolean", "HAS_MOBILEGLUES", hasMobileGlues.toString())
        buildConfigField("boolean", "HAS_MOBILEGL", hasMobileGl.toString())

        // The legacy (V1) plugin manifest, for launchers that predate the
        // renderer config above. It has no option UI, so the system backend.
        manifestPlaceholders.putAll(legacyManifest {
            displayName = "SFPEW"
            rendererName = "SFPEW"
            rendererLib = "libSimpleFPEWrapper.so"
            eglLib = "/libSimpleFPEWrapper.so"
            minMCVer = ""
            maxMCVer = ""
            boatEnv {
                put("LIBGL_ES", "3")
                put("POJAV_RENDERER", "opengles3")
            }
            pojavEnv {
                put("LIBGL_ES", "3")
                put("POJAV_RENDERER", "opengles3")
            }
        })
        manifestPlaceholders["appLabel"] = "SFPEW"

        ndk {
            val abis = (findProperty("sfpew.abis") ?: System.getenv("SFPEW_ABIS") ?: "arm64-v8a").toString()
            abiFilters += if (abis.equals("all", ignoreCase = true)) listOf("arm64-v8a", "armeabi-v7a", "x86_64")
            else abis.split(',').map(String::trim).filter(String::isNotEmpty)
        }
    }

    buildFeatures {
        resValues = true
        buildConfig = true
    }

    signingConfigs {
        if (releaseSigningReady) {
            create("release") {
                storeFile = signingStoreFile
                storePassword = signingStorePassword
                keyAlias = signingKeyAlias
                keyPassword = signingKeyPassword
            }
        }
    }

    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
            // Without the release key (a fork, a local build) the debug key,
            // so the APK still installs.
            signingConfig = if (releaseSigningReady) signingConfigs.getByName("release") else signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    packaging {
        jniLibs {
            // Launchers dlopen the renderer libraries by path out of
            // nativeLibraryDir, so they must be extracted there.
            useLegacyPackaging = true
            // A by-product of MobileGL's build that no packaged library loads.
            excludes += "**/libSPIRV-Tools-shared.so"
        }
    }
}

android.applicationVariants.configureEach {
    outputs.configureEach {
        (this as ApkVariantOutputImpl).outputFileName = "SFPEW-plugin-$name-$apkSuffix.apk"
    }
}

dependencies {
    implementation(project(":SimpleFPEWrapper"))
    if (hasMobileGlues) implementation(project(":MobileGlues"))
    if (hasMobileGl) implementation(project(":MobileGL"))
}

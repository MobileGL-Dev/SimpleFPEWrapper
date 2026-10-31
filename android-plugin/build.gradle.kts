import com.android.build.gradle.LibraryExtension

plugins {
    id("com.android.application") version "8.12.3" apply false
    id("com.android.library") version "8.12.3" apply false
}

// The ABIs the native libraries are built for: arm64-v8a unless
// -Psfpew.abis (a comma list, or "all") says otherwise. Applied to every
// library module, the wrapper's and the backends', because a library module
// otherwise builds every ABI the NDK has whatever the APK packages - MobileGL
// alone four times over.
fun Project.sfpewAbiFilters(): List<String> {
    val abis = (findProperty("sfpew.abis") ?: System.getenv("SFPEW_ABIS") ?: "arm64-v8a").toString()
    return if (abis.equals("all", ignoreCase = true)) {
        listOf("arm64-v8a", "armeabi-v7a", "x86_64")
    } else {
        abis.split(',').map(String::trim).filter(String::isNotEmpty)
    }
}

// An optional compiler launcher (ccache in CI) for every native build.
fun Project.sfpewCompilerLauncher(): String =
    (findProperty("sfpew.cmakeCompilerLauncher") ?: System.getenv("SFPEW_CMAKE_COMPILER_LAUNCHER") ?: "").toString().trim()

subprojects {
    plugins.withId("com.android.library") {
        extensions.configure<LibraryExtension> {
            defaultConfig {
                ndk {
                    abiFilters.clear()
                    abiFilters += sfpewAbiFilters()
                }
                sfpewCompilerLauncher().takeIf(String::isNotEmpty)?.let { launcher ->
                    externalNativeBuild {
                        cmake {
                            arguments += listOf(
                                "-DCMAKE_C_COMPILER_LAUNCHER=$launcher",
                                "-DCMAKE_CXX_COMPILER_LAUNCHER=$launcher",
                            )
                        }
                    }
                }
            }
        }
    }
}

// The wrapper's CMake project also builds the on-device test executables;
// the plugin packages the library alone.
project(":SimpleFPEWrapper") {
    plugins.withId("com.android.library") {
        extensions.configure<LibraryExtension> {
            defaultConfig {
                externalNativeBuild {
                    cmake {
                        targets += "SimpleFPEWrapper"
                    }
                }
            }
        }
    }
}

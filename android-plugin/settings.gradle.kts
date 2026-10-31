pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "SFPEWPlugin"
include(":app", ":SimpleFPEWrapper")
project(":SimpleFPEWrapper").projectDir = file("..")

// The backends besides the system's GLES/EGL, each built from its own source
// checkout when one is there: MobileGlues (its main branch) and MobileGL (its
// dev branch), at backends/<name> unless -Psfpew.<name>Dir or
// SFPEW_<NAME>_DIR points elsewhere. A backend whose checkout is absent is
// left out of the APK and out of the renderer's backend choice
// (app/build.gradle.kts), so the plugin still builds with the system
// backend alone.
fun includeBackend(project: String, property: String, variable: String) {
    val dir = file(
        providers.gradleProperty(property).orNull
            ?: System.getenv(variable)
            ?: "backends/$project",
    )
    if (dir.resolve("build.gradle").isFile || dir.resolve("build.gradle.kts").isFile) {
        include(":$project")
        project(":$project").projectDir = dir
    }
}
includeBackend("MobileGlues", "sfpew.mobilegluesDir", "SFPEW_MOBILEGLUES_DIR")
includeBackend("MobileGL", "sfpew.mobileglDir", "SFPEW_MOBILEGL_DIR")

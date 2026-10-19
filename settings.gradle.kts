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

rootProject.name = "ProtectedLinuxBridge"

include(":app")

// Developer-only APK: the activity refuses the Galaxy host before creating EGL.
include(":guest-gpu-probe")

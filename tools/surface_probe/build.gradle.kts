import java.util.Properties

plugins { id("com.android.application") version "9.3.2" }

val signing =
  Properties().apply {
    providers.environmentVariable("PROTECTED_LINUX_SIGNING_PROPERTIES").orNull?.let {
      file(it).inputStream().use { input -> load(input) }
    }
  }

android {
  namespace = "net.ryuya_dev.protectedlinux.surfaceprobe"
  compileSdk = 37
  defaultConfig {
    applicationId = "net.ryuya_dev.protectedlinux.surfaceprobe"
    minSdk = 36
    targetSdk = 36
    versionCode = 1
    versionName = "0.1-surface-probe"
  }
  signingConfigs {
    if (!signing.isEmpty)
      create("distribution") {
        storeFile = file(signing.getProperty("storeFile"))
        storePassword = signing.getProperty("storePassword")
        keyAlias = signing.getProperty("keyAlias")
        keyPassword = signing.getProperty("keyPassword")
      }
  }
  buildTypes {
    release {
      isDebuggable = false
      isMinifyEnabled = true
      proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
      signingConfig = signingConfigs.findByName("distribution")
    }
  }
  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
}

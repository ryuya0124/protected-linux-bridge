import java.util.Properties

plugins { id("com.android.application") }

val signing =
  Properties().apply {
    providers.environmentVariable("PROTECTED_LINUX_SIGNING_PROPERTIES").orNull?.let {
      file(it).inputStream().use { input -> load(input) }
    }
  }

android {
  namespace = "net.ryuya_dev.protectedlinux.gpuprobe"
  compileSdk = 37
  defaultConfig {
    applicationId = "net.ryuya_dev.protectedlinux.gpuprobe"
    minSdk = 36
    targetSdk = 36
    versionCode = 1
    versionName = "1.0"
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
      isShrinkResources = true
      proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
      signingConfig = signingConfigs.findByName("distribution")
    }
  }
  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
  lint { abortOnError = true }
}

dependencies { testImplementation("junit:junit:4.13.2") }

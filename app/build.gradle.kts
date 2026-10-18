import java.util.Properties
plugins { id("com.android.application") }
val signing = Properties().apply { providers.environmentVariable("PROTECTED_LINUX_SIGNING_PROPERTIES").orNull?.let { file(it).inputStream().use { f -> load(f) } } }
android {
 namespace = "net.ryuya_dev.protectedlinux"
 compileSdk = 37
 defaultConfig { applicationId = "net.ryuya_dev.protectedlinux"; minSdk = 36; targetSdk = 36; versionCode = 3; versionName = "0.1.0-preview.2" }
 signingConfigs { if (!signing.isEmpty) create("distribution") { storeFile = file(signing.getProperty("storeFile")); storePassword = signing.getProperty("storePassword"); keyAlias = signing.getProperty("keyAlias"); keyPassword = signing.getProperty("keyPassword") } }
 buildTypes { release { isDebuggable = false; isMinifyEnabled = false; signingConfig = signingConfigs.findByName("distribution") } }
 compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
 lint { abortOnError = true }
}
dependencies { compileOnly("io.github.libxposed:api:102.0.0"); implementation("com.google.android.material:material:1.14.0"); implementation("io.github.libxposed:service:102.0.0"); testImplementation("junit:junit:4.13.2") }

val nativeAssets=layout.buildDirectory.dir("generated/nativeAssets")
android.sourceSets.getByName("main").assets.directories.add(nativeAssets.get().asFile.absolutePath)
val buildNative=tasks.register("buildNativeAssets") {
 inputs.dir(rootProject.file("native")); inputs.dir(rootProject.file("tools"))
 outputs.dir(nativeAssets)
 doLast {
  val local=Properties().apply { val p=rootProject.file("local.properties"); if(p.exists())p.inputStream().use {load(it)} }
  val sdk=local.getProperty("sdk.dir") ?: System.getenv("ANDROID_HOME") ?: System.getenv("ANDROID_SDK_ROOT") ?: error("Set Android SDK path")
  val process=ProcessBuilder("python3",rootProject.file("tools/build_native.py").absolutePath,"--sdk",sdk).inheritIO().start()
  check(process.waitFor()==0) { "Native build failed" }
 }
}
tasks.named("preBuild").configure {dependsOn(buildNative)}
val exportRuntimeComponents = tasks.register("exportRuntimeComponents") {
    doLast {
        val components = configurations.getByName("debugRuntimeClasspath").resolvedConfiguration.resolvedArtifacts
            .map { it.moduleVersion.id.toString() }.toSortedSet()
        val file = layout.buildDirectory.file("runtime-components.txt").get().asFile
        file.parentFile.mkdirs(); file.writeText(components.joinToString("\n") + "\n")
    }
}
val generatedLicenses = layout.buildDirectory.dir("generated/licenseAssets")
android.sourceSets.getByName("main").assets.directories.add(generatedLicenses.get().asFile.absolutePath)
val dependencyLicenses = tasks.register("generateDependencyLicenses") {
    dependsOn(exportRuntimeComponents)
    inputs.file(rootProject.file("third_party/components.tsv"))
    inputs.dir(rootProject.file("third_party"))
    inputs.file(rootProject.file("LICENSE"))
    inputs.file(layout.buildDirectory.file("runtime-components.txt"))
    outputs.dir(generatedLicenses)
    doLast {
        val rows = rootProject.file("third_party/components.tsv").readLines().filter { it.isNotBlank() && !it.startsWith("#") }.map { it.split('\t') }
        val licensed = rows.map { it[0] }.toSet()
        val actual = layout.buildDirectory.file("runtime-components.txt").get().asFile.readLines().filter { it.isNotBlank() }.toSet() + "io.github.libxposed:api:102.0.0"
        check(actual == licensed) { "Review dependency licenses: missing=${actual-licensed}; obsolete=${licensed-actual}" }
        val out = generatedLicenses.get().asFile; out.mkdirs()
        rootProject.file("LICENSE").copyTo(out.resolve("APP_LICENSE.txt"),true)
        val notices = buildString {
            append("Bundled runtime dependencies (including transitive dependencies), plus compile-only Xposed API.\n\n")
            append("AndroidX / Material: The Android Open Source Project and respective contributors.\nKotlin / coroutines / annotations: JetBrains and respective contributors.\nGuava / Error Prone / J2ObjC / JSpecify: Google and respective contributors.\nlibxposed API: libxposed contributors.\n\n")
            for (row in rows) append(row[0]+"\n"+row[1]+"\n"+row.getOrElse(3){""}+"\n\n")
            for (name in rows.map { it[2] }.distinct()) {
                append("\n===== $name =====\n\n")
                append(rootProject.file("third_party/$name").readText())
            }
        }
        out.resolve("THIRD_PARTY_NOTICES.txt").writeText(notices + "\n@xterm/xterm 5.5.0\n" + rootProject.file("third_party/xterm-LICENSE.txt").readText()+"\nGuest musl user-space runtime\n"+rootProject.file("third_party/musl-COPYRIGHT.txt").readText()+"\nCompiler runtime (Apache-2.0 WITH LLVM-exception)\n"+rootProject.file("third_party/compiler-rt-LICENSE.txt").readText()+"\nMusl component notices\n"+rootProject.file("third_party/musl-source-notices.txt").readText()+"\nnoVNC 1.6.0 (unmodified MPL-2.0 source modules)\n"+rootProject.file("third_party/novnc").walkTopDown().filter { it.isFile }.sortedBy { it.path }.joinToString("\n\n") {it.relativeTo(rootProject.file("third_party/novnc")).path+"\n"+it.readText()})
    }
}
tasks.named("preBuild").configure { dependsOn(dependencyLicenses) }

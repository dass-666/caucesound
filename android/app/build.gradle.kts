import java.security.MessageDigest
import java.util.Properties
import java.util.zip.ZipFile

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// --- Paquetes de fábrica reproducibles (misión 0.10) -----------------------------------------------------------
fun factoryPacksDir(): File? =
    (findProperty("caucePacks") as String? ?: System.getenv("CAUCE_PACKS"))?.let { file(it) }?.takeIf { File(it, "packs").isDirectory }

val packsLockFile: File get() = rootProject.file("../packs/factory-packs.lock.json")

@Suppress("UNCHECKED_CAST")
fun packsLock(): Map<String, Any?> = groovy.json.JsonSlurper().parse(packsLockFile) as Map<String, Any?>

@Suppress("UNCHECKED_CAST")
fun lockedPacks(): List<Map<String, Any?>> = packsLock()["packs"] as List<Map<String, Any?>>

fun sha256Of(f: File): String {
    val md = MessageDigest.getInstance("SHA-256")
    f.inputStream().use { input -> val buf = ByteArray(1 shl 16); while (true) { val n = input.read(buf); if (n < 0) break; md.update(buf, 0, n) } }
    return md.digest().joinToString("") { "%02x".format(it) }
}

/** Comprueba la carpeta de paquetes contra el bloqueo. Devuelve la lista de problemas (vacía = todo correcto). */
@Suppress("UNCHECKED_CAST")
fun checkFactoryPacks(dir: File): List<String> {
    val problems = mutableListOf<String>()
    val root = File(dir, "packs")
    val lock = packsLock()
    val locked = lockedPacks()
    var total = 0L
    for (p in locked) {
        val id = p["id"] as String
        val pd = File(root, id)
        val mf = File(pd, "cauce-pack.json")
        if (!mf.isFile) { problems += "$id: falta el paquete (no existe ${mf.path})"; continue }
        if (sha256Of(mf) != p["manifestSha256"]) { problems += "$id: el manifiesto no es el del bloqueo (¿otra versión? se espera ${p["version"]})"; continue }
        val m = groovy.json.JsonSlurper().parse(mf) as Map<String, Any?>
        if (m["license"] != p["license"]) problems += "$id: licencia «${m["license"]}» distinta de la registrada «${p["license"]}»"
        val licenseFile = m["licenseFile"] as String?
        if (licenseFile == null || !File(pd, licenseFile).isFile || File(pd, licenseFile).length() == 0L) problems += "$id: falta el texto de la licencia"
        if (!File(pd, "PROVENANCE.md").isFile) problems += "$id: falta PROVENANCE.md (procedencia de cada muestra)"
        val sources = m["sources"] as List<Map<String, Any?>>?
        if (sources.isNullOrEmpty() || sources.any { (it["license"] as String?).isNullOrBlank() || (it["url"] as String?).isNullOrBlank() })
            problems += "$id: el manifiesto no declara el origen y la licencia de todas sus fuentes"
        val declared = (m["files"] as List<Map<String, Any?>>).associateBy { it["path"] as String }
        for ((path, f) in declared) {
            val file = File(pd, path)
            if (!file.isFile) { problems += "$id: falta $path"; continue }
            if (file.length() != (f["bytes"] as Number).toLong()) { problems += "$id: tamaño distinto en $path"; continue }
            if (sha256Of(file) != f["sha256"]) problems += "$id: SHA-256 distinto en $path (archivo alterado o dañado)"
            total += file.length()
        }
        total += mf.length()
        val onDisk = pd.walkTopDown().filter { it.isFile }.map { it.relativeTo(pd).invariantSeparatorsPath }.toSet()
        (onDisk - declared.keys - "cauce-pack.json").forEach { problems += "$id: archivo no declarado $it" }
        val instruments = (m["instruments"] as List<Map<String, Any?>>).map { it["id"] as String }.toSet()
        ((p["instruments"] as List<String>).toSet() - instruments).forEach { problems += "$id: falta el instrumento $it (lo usan proyectos guardados)" }
    }
    val extra = (root.listFiles()?.filter { it.isDirectory }?.map { it.name }?.toSet() ?: emptySet()) - locked.map { it["id"] as String }.toSet()
    extra.forEach { problems += "$it: paquete no registrado en packs/factory-packs.lock.json (no se incluye nada sin registrar)" }
    val max = (lock["maxTotalBytes"] as Number).toLong()
    if (total > max) problems += "los paquetes pesan $total bytes y el límite acordado es $max (maxTotalBytes): el APK crecería sin decidirlo"
    return problems
}

fun failPacks(problems: List<String>): Nothing = throw GradleException(
    "PAQUETES DE SONIDO DE FÁBRICA: la compilación se detiene para no producir un APK sin sus sonidos.\n" +
        problems.joinToString("\n") { "  - $it" } +
        "\nCómo arreglarlo: python3 tools/factory_packs.py prepare <carpeta con los ZIP> <salida> y compila con CAUCE_PACKS=<salida>.")

// Con paquetes presentes siempre se verifican; sin ellos solo se permite la variante debug (pruebas y CI).
val verifyFactoryPacks by tasks.registering {
    group = "verification"
    description = "Comprueba los paquetes de fábrica (presencia, SHA-256, licencia, procedencia, tamaño) contra el bloqueo."
    inputs.file(packsLockFile)
    doLast {
        val dir = factoryPacksDir() ?: failPacks(listOf("no se indicó CAUCE_PACKS (o -PcaucePacks) con una carpeta que contenga packs/<id>/"))
        val problems = checkFactoryPacks(dir)
        if (problems.isNotEmpty()) failPacks(problems)
        logger.lifecycle("Paquetes de fábrica verificados: " + lockedPacks().joinToString { "${it["id"]} ${it["version"]}" })
    }
}
val verifyFactoryPacksIfPresent by tasks.registering {
    group = "verification"
    doLast { factoryPacksDir()?.let { val problems = checkFactoryPacks(it); if (problems.isNotEmpty()) failPacks(problems) } }
}

/** Tras empaquetar: abre el APK y comprueba que cada archivo de cada paquete está dentro con su tamaño. */
@Suppress("UNCHECKED_CAST")
fun checkApkPacks(apk: File, dir: File): List<String> {
    val problems = mutableListOf<String>()
    ZipFile(apk).use { zip ->
        for (p in lockedPacks()) {
            val id = p["id"] as String
            val m = groovy.json.JsonSlurper().parse(File(dir, "packs/$id/cauce-pack.json")) as Map<String, Any?>
            val paths = (m["files"] as List<Map<String, Any?>>).map { (it["path"] as String) to (it["bytes"] as Number).toLong() } + ("cauce-pack.json" to File(dir, "packs/$id/cauce-pack.json").length())
            for ((path, bytes) in paths) {
                val e = zip.getEntry("assets/packs/$id/$path")
                if (e == null) problems += "$id: $path no está dentro del APK" else if (e.size != bytes) problems += "$id: $path tiene otro tamaño dentro del APK"
            }
        }
    }
    return problems
}

afterEvaluate {
    listOf("Preview", "Release").forEach { v ->
        tasks.findByName("merge${v}Assets")?.dependsOn(verifyFactoryPacks)
        tasks.findByName("package$v")?.let { pkg ->
            val check = tasks.register("verify${v}ApkPacks") {
                group = "verification"
                doLast {
                    val dir = factoryPacksDir() ?: failPacks(listOf("no se indicó CAUCE_PACKS"))
                    val apks = layout.buildDirectory.dir("outputs/apk/${v.lowercase()}").get().asFile.listFiles { f -> f.extension == "apk" }.orEmpty()
                    if (apks.isEmpty()) failPacks(listOf("no se encontró el APK $v para comprobarlo"))
                    apks.forEach { apk -> val problems = checkApkPacks(apk, dir); if (problems.isNotEmpty()) failPacks(problems); logger.lifecycle("APK con sus paquetes de fábrica: ${apk.name}") }
                }
            }
            pkg.finalizedBy(check)
        }
    }
    tasks.findByName("mergeDebugAssets")?.dependsOn(verifyFactoryPacksIfPresent)
}


android {
    namespace = "com.caucesound.app"
    compileSdk = 36
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.caucesound.app"
        minSdk = 26          // Android 8.0: AAudio disponible; Oboe elige AAudio u OpenSL ES según el dispositivo
        targetSdk = 36
        versionCode = 14
        versionName = "0.9.2-hotfix"
        manifestPlaceholders["appLabel"] = "CAUCESOUND"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") } // teléfonos actuales + emulador
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared", "-DCS_BUILD_TESTS=OFF")
                cppFlags += listOf("-std=c++17", "-fexceptions", "-frtti")
            }
        }
    }

    // Clave de PRUEBA persistente (no comercial, fuera de git). Ruta del .properties en CAUCE_SIGNING o -PcauceSigning.
    // Sin ella, la variante preview se firma con la clave de depuración local (no se podrá actualizar entre máquinas).
    val signingProps = (findProperty("cauceSigning") as String? ?: System.getenv("CAUCE_SIGNING"))
        ?.let { file(it) }?.takeIf { it.exists() }
        ?.let { f -> Properties().apply { f.inputStream().use { load(it) } } }
    signingConfigs {
        if (signingProps != null) create("preview") {
            storeFile = file(signingProps.getProperty("storeFile"))
            storePassword = signingProps.getProperty("storePassword")
            keyAlias = signingProps.getProperty("keyAlias")
            keyPassword = signingProps.getProperty("keyPassword")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Antes de publicar: clave de subida propia y Play App Signing (decisión pendiente del propietario).
            signingConfig = signingConfigs.getByName("debug")
        }
        // APK de pruebas en el teléfono: otro applicationId para instalarse JUNTO a la app de la fase 1
        // (aquella se firmó con una clave de depuración perdida y no se puede actualizar).
        create("preview") {
            initWith(getByName("release"))
            applicationIdSuffix = ".preview"
            versionNameSuffix = "-preview"
            manifestPlaceholders["appLabel"] = "CAUCESOUND Preview"
            signingConfig = signingConfigs.findByName("preview") ?: signingConfigs.getByName("debug")
            matchingFallbacks += listOf("release")
        }
    }
    // Paquetes de fábrica: se generan con desktop/tools/packbuilder y NO están en git (las muestras de terceros se
    // regeneran desde su origen). Carpeta con packs/<id>/ en CAUCE_PACKS o -PcaucePacks (la prepara
    // tools/factory_packs.py). Desde la 0.10 las variantes preview y release NO se compilan sin ellos: la tarea
    // verifyFactoryPacks comprueba cada archivo contra packs/factory-packs.lock.json (ver más abajo).
    factoryPacksDir()?.let { dir -> sourceSets.getByName("main").assets.srcDir(dir) }
    defaultConfig {
        // La app sabe qué sonidos promete esta compilación y avisa con claridad si al arrancar no los encuentra.
        buildConfigField("String", "FACTORY_PACKS", "\"" + (if (factoryPacksDir() != null) lockedPacks().joinToString(",") { it["id"] as String } else "") + "\"")
    }
    androidResources { noCompress += listOf("wav") } // el motor copia los WAV tal cual; comprimirlos apenas ahorra
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }
    buildFeatures {
        compose = true
        prefab = true
        buildConfig = true
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    testOptions {
        unitTests.isReturnDefaultValues = true
        unitTests.isIncludeAndroidResources = true // Robolectric: pruebas de interfaz Compose en la JVM
        unitTests.all { t ->
            // Capturas de pantalla reales (renderizadas por Robolectric en modo nativo de gráficos) solo si se piden:
            //   ./gradlew testDebugUnitTest -Pscreenshots   → app/build/outputs/roborazzi/*.png
            if (project.hasProperty("screenshots")) t.systemProperty("roborazzi.test.record", "true")
            // Espejo opcional para descargar android-all en entornos sin acceso a Maven Central.
            System.getenv("ROBOLECTRIC_REPO_URL")?.let { t.systemProperty("robolectric.dependency.repo.url", it) }
            // Hotfix 0.9.2: pruebas de extremo a extremo con el puente JNI real compilado para el ordenador
            // (cmake -DCS_BUILD_HOST_JNI=ON). Sin la variable, EndToEndVariationsTest se salta.
            System.getenv("CS_HOST_JNI_DIR")?.let { t.systemProperty("java.library.path", it); t.systemProperty("cs.hostJni", it) }
            t.maxHeapSize = "2g"
        }
    }
}

dependencies {
    implementation("com.google.oboe:oboe:1.10.0")
    implementation(platform("androidx.compose:compose-bom:2025.12.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.12.4")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.9.4")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.9.4")
    implementation("androidx.core:core-ktx:1.17.0")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")
    testImplementation("org.robolectric:robolectric:4.16.1")
    testImplementation("io.github.takahirom.roborazzi:roborazzi:1.70.0")
    testImplementation("io.github.takahirom.roborazzi:roborazzi-compose:1.70.0")
    testImplementation(platform("androidx.compose:compose-bom:2025.12.01"))
    testImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}

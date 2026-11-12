import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.caucesound.app"
    compileSdk = 36
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.caucesound.app"
        minSdk = 26          // Android 8.0: AAudio disponible; Oboe elige AAudio u OpenSL ES según el dispositivo
        targetSdk = 36
        versionCode = 5
        versionName = "0.4.0-sound"
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
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }
    buildFeatures {
        compose = true
        prefab = true
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

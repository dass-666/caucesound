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
        versionCode = 1
        versionName = "0.1.0-fase1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") } // teléfonos actuales + emulador
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared", "-DCS_BUILD_TESTS=OFF")
                cppFlags += listOf("-std=c++17", "-fexceptions", "-frtti")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Fase 1: se firma con la clave de depuración para poder instalarlo. Antes de publicar: clave propia.
            signingConfig = signingConfigs.getByName("debug")
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

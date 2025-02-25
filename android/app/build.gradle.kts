import java.time.LocalDateTime
import java.time.Month
import java.time.temporal.ChronoUnit

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "su.xash.engine"
    ndkVersion = "28.0.13004108"

    defaultConfig {
        applicationId = "su.xash"
        applicationIdSuffix = "engine"
        versionName = "0.21"
        versionCode = getBuildNum()
        minSdk = 21
        targetSdk = 34
        compileSdk = 34

        externalNativeBuild {
            cmake {
                abiFilters("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
                arguments("-DANDROID_USE_LEGACY_TOOLCHAIN_FILE=OFF")
            }
        }
    }

    externalNativeBuild {
        cmake {
            version = "3.22.1"
            path = file("CMakeLists.txt")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = "1.8"
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
            isShrinkResources = false
            isDebuggable = true
            applicationIdSuffix = ".test"
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro"
            )
        }

        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro"
            )
        }

        register("asan") {
            initWith(getByName("debug"))
        }

        register("continuous") {
            initWith(getByName("release"))
            applicationIdSuffix = ".test"
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDirs("../../3rdparty/extras/xash-extras", "../moddb")
            java.srcDir("../../3rdparty/SDL/android-project/app/src/main/java")
        }
    }

    lint {
        abortOnError = false
    }

    buildFeatures {
        viewBinding = true
        buildConfig = true
    }

    androidResources {
        noCompress += ""
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
            keepDebugSymbols.add("**/*.so")
        }
    }
}

dependencies {
    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.navigation:navigation-fragment-ktx:2.7.7")
    implementation("androidx.navigation:navigation-ui-ktx:2.7.7")
    implementation("androidx.cardview:cardview:1.0.0")
    implementation("androidx.annotation:annotation:1.7.1")
    implementation("androidx.fragment:fragment-ktx:1.6.2")
    implementation("androidx.preference:preference-ktx:1.2.1")
    implementation("androidx.swiperefreshlayout:swiperefreshlayout:1.1.0")
    implementation("androidx.work:work-runtime-ktx:2.9.0")
//  implementation "androidx.legacy:legacy-support-v4:1.0.0"

    implementation("com.madgag.spongycastle:prov:1.58.0.0")
    implementation("in.dragonbra:javasteam:1.2.0")

    implementation("ch.acra:acra-http:5.11.2")
}

fun getBuildNum(): Int {
    val now = LocalDateTime.now()
    val releaseDate = LocalDateTime.of(2015, Month.APRIL, 1, 0, 0, 0)
    val qBuildNum = releaseDate.until(now, ChronoUnit.DAYS)
    val minuteOfDay = now.hour * 60 + now.minute
    return (qBuildNum * 10000 + minuteOfDay).toInt()
}

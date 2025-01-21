import java.time.LocalDateTime
import java.time.Month
import java.time.temporal.ChronoUnit

plugins {
	alias(libs.plugins.android.application)
	alias(libs.plugins.kotlin.android)
}

android {
    namespace = "su.xash.engine"
	ndkVersion = "28.0.13004108"
	compileSdk = 35

    defaultConfig {
        applicationId = "su.xash.engine"
        versionName = "0.21"
        versionCode = getBuildNum()
        minSdk = 21
        targetSdk = 35
    }

    compileOptions {
		sourceCompatibility = JavaVersion.VERSION_11
		targetCompatibility = JavaVersion.VERSION_11
    }

	kotlinOptions {
		jvmTarget = "11"
	}

	externalNativeBuild {
		cmake {
			path = file("CMakeLists.txt")
			version = "3.22.1"
		}
	}

	buildFeatures {
		viewBinding = true
		buildConfig = true
	}

	lint {
		abortOnError = false
	}

	androidResources {
		noCompress += ""
	}

	sourceSets {
		getByName("main") {
			assets.srcDirs("../../3rdparty/extras/xash-extras")
			java.srcDir("../../3rdparty/SDL/android-project/app/src/main/java")
		}
	}

	packaging {
		jniLibs {
			keepDebugSymbols.add("**/*.so")
		}
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
}

dependencies {
	implementation(libs.material)

	implementation(libs.appcompat)
	implementation(libs.navigation.runtime.ktx)
	implementation(libs.navigation.fragment.ktx)
	implementation(libs.navigation.ui.ktx)
	implementation(libs.preference.ktx)
	implementation(libs.swiperefreshlayout)

    implementation(libs.acra.http)
}

fun getBuildNum(): Int {
    val now = LocalDateTime.now()
    val releaseDate = LocalDateTime.of(2015, Month.APRIL, 1, 0, 0, 0)
    val qBuildNum = releaseDate.until(now, ChronoUnit.DAYS)
    val minuteOfDay = now.hour * 60 + now.minute
    return (qBuildNum * 10000 + minuteOfDay).toInt()
}

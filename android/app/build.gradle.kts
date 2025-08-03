import com.google.firebase.crashlytics.buildtools.gradle.CrashlyticsExtension
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.time.LocalDateTime
import java.time.Month
import java.time.temporal.ChronoUnit

plugins {
	alias(libs.plugins.android.application)
	alias(libs.plugins.kotlin.android)
	alias(libs.plugins.compose.compiler)
	alias(libs.plugins.google.services)
	alias(libs.plugins.firebase.crashlytics)
	alias(libs.plugins.kotlin.serialization)
	alias(libs.plugins.hilt.android)
	alias(libs.plugins.ksp)
}

android {
	namespace = "su.xash.engine"
	ndkVersion = "28.2.13676358"
	compileSdk = 36

	defaultConfig {
		applicationId = "su.xash.engine"
		versionName = "0.21-" + getGitHash()
		versionCode = getBuildNum()
		minSdk = 23
		targetSdk = 36

		externalNativeBuild {
			val engineRoot = projectDir.parentFile.parent

			experimentalProperties["ninja.abiFilters"] = setOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
			experimentalProperties["ninja.path"] = File(engineRoot, "wscript").path
			experimentalProperties["ninja.configure"] = "run-python"
			experimentalProperties["ninja.arguments"] = setOf(
				File(engineRoot, "scripts/configure-ninja.py").path,
				engineRoot,
				"--variant=\${ndk.variantName}",
				"--abi=\${ndk.abi}",
				"--configuration-dir=\${ndk.buildRoot}",
				"--ndk-version=\${ndk.moduleNdkVersion}",
				"--min-sdk-version=\${ndk.minPlatform}",
				"--ndk-root=${android.ndkDirectory}",
				// shut up, fake options
				"-p:Configuration=\${ndk.variantName}",
				"-p:Platform=\${ndk.abi}"
			)
		}
	}

	buildTypes {
		debug {
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

			configure<CrashlyticsExtension> {
				nativeSymbolUploadEnabled = true
			}
		}

		create("asan") {
			initWith(getByName("debug"))
		}

		create("continuous") {
			initWith(getByName("release"))
			applicationIdSuffix = ".test"
		}
	}

	flavorDimensions += "version"

	productFlavors {
		create("googlePlay") {
			dimension = "version"
			applicationId = "in.celest.xash3d.hl"
			buildConfigField("Boolean", "IS_GOOGLE_PLAY_BUILD", "true")
		}

		create("git") {
			dimension = "version"
			applicationId = "su.xash.engine"
			buildConfigField("Boolean", "IS_GOOGLE_PLAY_BUILD", "false")
		}
	}

	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_11
		targetCompatibility = JavaVersion.VERSION_11
	}

	kotlin {
		compilerOptions {
			jvmTarget = JvmTarget.JVM_11
		}
	}

	buildFeatures {
		compose = true
		buildConfig = true
	}

	lint {
		abortOnError = false
	}

/*
	androidResources {
		noCompress += ""
	}
*/

	packaging {
		jniLibs {
			keepDebugSymbols.add("**/*.so")
			useLegacyPackaging = true
		}
	}

	sourceSets {
		getByName("main") {
			assets.srcDirs("../../3rdparty/extras/xash-extras")
			java.srcDir("../../3rdparty/SDL/android-project/app/src/main/java")
		}
	}
}

dependencies {
	implementation(platform(libs.compose.bom))
	implementation(libs.material3)
	implementation(libs.ui.tooling)
	implementation(libs.activity.compose)
	implementation(libs.navigation.compose)
	ksp(libs.hilt.compiler)
	implementation(libs.hilt.android)
	implementation(libs.appcompat)
	implementation(libs.lifecycle.viewmodel.ktx)
	implementation(libs.datastore.preferences)
	implementation(libs.coil.compose)
	implementation(libs.hilt.navigation.compose)
	implementation(libs.accompanist.permissions)
	implementation(libs.material.icons.extended)
	implementation(platform(libs.firebase.bom))
	implementation(libs.firebase.crashlytics.ndk)
	implementation(libs.firebase.analytics)
}

fun getBuildNum(): Int {
	val now = LocalDateTime.now()
	val releaseDate = LocalDateTime.of(2015, Month.APRIL, 1, 0, 0, 0)
	val qBuildNum = releaseDate.until(now, ChronoUnit.DAYS)
	val minuteOfDay = now.hour * 60 + now.minute
	return (qBuildNum * 10000 + minuteOfDay).toInt()
}

fun getGitHash(): String {
	val process = ProcessBuilder("git", "rev-parse", "--short", "HEAD").directory(project.rootDir)
		.redirectErrorStream(true).start()
	return process.inputStream.bufferedReader().readText().trim()
}

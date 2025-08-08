import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.time.LocalDateTime
import java.time.Month
import java.time.temporal.ChronoUnit

plugins {
	alias(libs.plugins.android.application)
	alias(libs.plugins.kotlin.android)
}

android {
	namespace = "su.xash.engine"
	ndkVersion = "28.2.13676358"
	compileSdk = 35

	defaultConfig {
		applicationId = "su.xash.engine"
		versionName = "0.21-" + getGitHash()
		versionCode = getBuildNum()
		minSdk = 21
		targetSdk = 35

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
		viewBinding = true
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

fun getGitHash(): String {
	val process = ProcessBuilder("git", "rev-parse", "--short", "HEAD").directory(project.rootDir)
		.redirectErrorStream(true).start()
	return process.inputStream.bufferedReader().readText().trim()
}

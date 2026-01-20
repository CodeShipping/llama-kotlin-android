plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    id("maven-publish")
    id("signing")
}

android {
    namespace = "org.codeshipping.llamakotlin"
    compileSdk = 36

    ndkVersion = "27.3.13750724"

    defaultConfig {
        minSdk = 24

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.pro")

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-fexceptions", "-frtti")
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_ARM_NEON=TRUE",
                    "-DLLAMA_BUILD_TESTS=OFF",
                    "-DLLAMA_BUILD_EXAMPLES=OFF",
                    "-DLLAMA_BUILD_SERVER=OFF"
                )
            }
        }

        ndk {
            // Only 64-bit architectures - armv7 has issues with float16 NEON in llama.cpp
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        buildConfig = true
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
            withJavadocJar()
        }
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.androidx.annotation)
    
    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}

publishing {
    publications {
        register<MavenPublication>("release") {
            groupId = "org.codeshipping"
            artifactId = "llama-kotlin-android"
            version = "0.1.0"

            afterEvaluate {
                from(components["release"])
            }

            pom {
                name.set("LLaMA Kotlin Android")
                description.set("A Kotlin-first Android library for running LLaMA models on-device using llama.cpp")
                url.set("https://github.com/codeshipping/llama-kotlin-android")

                licenses {
                    license {
                        name.set("MIT License")
                        url.set("https://opensource.org/licenses/MIT")
                    }
                }

                developers {
                    developer {
                        id.set("it5prasoon")
                        name.set("Prasoon Kumar")
                        email.set("prasoonk187@gmail.com")
                    }
                }

                scm {
                    connection.set("scm:git:git://github.com/codeshipping/llama-kotlin-android.git")
                    developerConnection.set("scm:git:ssh://github.com/codeshipping/llama-kotlin-android.git")
                    url.set("https://github.com/codeshipping/llama-kotlin-android")
                }
            }
        }
    }

    repositories {
        maven {
            name = "local"
            url = uri(layout.buildDirectory.dir("repo"))
        }
    }
}

signing {
    val signingKeyId = findProperty("signing.keyId") as String?
    val signingPassword = findProperty("signing.password") as String?
    val signingKeyFile = file(System.getProperty("user.home") + "/personal-workspace/Importants/gpg-maven-signing-key.asc")
    
    if (signingKeyFile.exists()) {
        useInMemoryPgpKeys(
            signingKeyId,
            signingKeyFile.readText(),
            signingPassword
        )
        sign(publishing.publications["release"])
    }
}

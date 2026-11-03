plugins {
    id("com.android.application")
}

android {
    namespace = "com.steamwifi.controller"
    compileSdk = 35

    defaultConfig {
        // Identificador do nome antigo: o JoyHub atualiza o app instalado e preserva as preferências.
        applicationId = "com.steamwifi.controller"
        minSdk = 26
        targetSdk = 35
        versionCode = 3
        versionName = "1.2"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            // Assinado com a chave de debug para poder instalar direto (uso pessoal).
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

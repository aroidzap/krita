# Building Krita for Android

First of all, I use linux to do my builds and testing. Although,
they _should_ work on Windows/macOS, I cannot give any guarantee 
that it will.

## Setting up Android SDK and NDK

Android SDK. You can either download Android Studio or just
the `sdk-tools` (Command line tools) . Both could be downloaded from [google's website](https://developer.android.com/studio).

If you downloaded Android Studio then open SDK manager and download
`Android SDK Build-Tools`.
(more info: https://developer.android.com/studio/intro/update#sdk-manager)

If you download just `sdk-tools` (Command line tools), then, extract it and run:

```shell
cd <extracted-android-sdk-tools>/tools/bin
./sdkmanager --sdk_root=${HOME}/Android/Sdk --licenses
./sdkmanager --sdk_root=${HOME}/Android/Sdk platform-tools
./sdkmanager --sdk_root=${HOME}/Android/Sdk "platforms;android-21"
./sdkmanager --sdk_root=${HOME}/Android/Sdk "platforms;android-28"    # for androiddeployqt
./sdkmanager --sdk_root=${HOME}/Android/Sdk "build-tools;28.0.2"
./sdkmanager --sdk_root=${HOME}/Android/Sdk ndk-bundle
```

If you get some `ClasNotFoundException` it might be because `java`
version is set to `11`. For `sdkmanager` to work, set it to `8` and
then run it again.

```
sudo apt-get install -y openjdk-8-jdk
sudo update-alternatives --set java /usr/lib/jvm/java-8-openjdk-amd64/jre/bin/java
```

That's the only dependency we have to manage manually!

## Building Krita

Now, to build krita, run `<krita-source>/packaging/android/androidbuild.sh --help`
and pass the required arguments.

Example:

```shell
./androidbuild.sh -p=all --src=${HOME}/repos/krita --build-type=Debug --build-root=${HOME}/repos/krita/android-build --ndk-path=${HOME}/Android/Sdk/ndk-bundle --sdk-path=${HOME}/Android/Sdk --api-level=21 --android-abi=armeabi-v7a
```

That's all!

## Installing Krita APK

To install run `adb install -d -r <build-root>/krita_build_apk/build/outputs/apk/debug/krita_build_apk-debug.apk`.

`adb` should be in `<sdk-root>/platform-tools/`

## Crash

If Krita crashes you can look up the logs using `adb logcat`

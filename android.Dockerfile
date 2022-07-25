# --- Simple build ---
# git clone --recurse-submodules https://invent.kde.org/graphics/krita.git
# cd krita
# docker build . -f android.Dockerfile -t krita:android
# docker run --rm -it -v ${PWD}:/home/kritadev/krita krita:android /home/kritadev/build.sh Debug arm64-v8a all

# --- Install apk ---
# Download platform-tools https://developer.android.com/studio/releases/platform-tools
# Enable developer mode on your tablet, enable usb debugging, connect your tablet to PC
# Run `adb devices` to start service, allow usb debugging when prompted on tablet
# Install apk using adb:
#     adb install -d -r ${PWD}/build/krita_build_apk/build/outputs/apk/debug/krita*.apk

# --- Advanced build ---
# Run detached container:
#    docker run -d --name kritadev --rm -it -v ${PWD}:/home/kritadev/krita krita:android
# Attach to running container using vscode remote containers or:
#    docker exec -it kritadev /bin/bash
# Build:
#    docker exec -it kritadev /bin/bash -c './build.sh Debug arm64-v8a all'
# Build (without reconfigure):
#    docker exec -it kritadev /bin/bash -c 'cd krita/build && make -j16 install'
# Remove detached container:
#    docker rm -f kritadev

# ---------------------------------------------------------------------------------

FROM ubuntu:20.04

ARG USER=kritadev
RUN groupadd -g 1000 -r $USER
RUN useradd -u 1000 -g 1000 --create-home -r $USER

RUN apt-get update && \ 
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
    openjdk-8-jdk git git-lfs unzip gettext \
    python3 python3-pip python-is-python3 && \
    apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

RUN update-alternatives --set java /usr/lib/jvm/java-8-openjdk-amd64/jre/bin/java

RUN pip3 install cmake==3.22.5 requests

RUN CMDLINE_TOOLS=$(python -c 'import requests,re;print(re.findall(\
    "commandlinetools-linux-\d+_latest.zip",requests.get(\
    "https://developer.android.com/studio/index.html").text)[0],end="")') && \
    curl -L https://dl.google.com/android/repository/${CMDLINE_TOOLS} -o /tmp/commandlinetools-linux.zip && \
    unzip /tmp/commandlinetools-linux.zip -d /opt/ && \
    rm -rf /tmp/*

# ndk-r22b
RUN SDKMANAGER=/opt/cmdline-tools/bin/sdkmanager && \
    yes | ${SDKMANAGER} --sdk_root=/opt/android-sdk --licenses && \
    ${SDKMANAGER} --sdk_root=/opt/android-sdk platform-tools && \
    ${SDKMANAGER} --sdk_root=/opt/android-sdk "platforms;android-21" && \
    ${SDKMANAGER} --sdk_root=/opt/android-sdk "platforms;android-30" && \
    ${SDKMANAGER} --sdk_root=/opt/android-sdk "build-tools;29.0.2" && \
    ${SDKMANAGER} --sdk_root=/opt/android-sdk ndk-bundle

USER $USER
WORKDIR /home/$USER

RUN ( \
    echo "#!/bin/bash"; \
    echo "BUILD_TYPE=\${1:-Debug}"; \
    echo "ANDROID_ABI=\${2:-arm64-v8a}"; \
    echo "PACKAGES=\${3:-all}"; \
    echo "$HOME/krita/packaging/android/androidbuild.sh \\"; \
    echo "    -p=\${PACKAGES} \\"; \
    echo "    --src=$HOME/krita \\"; \
    echo "    --build-type=\${BUILD_TYPE} \\"; \
    echo "    --build-root=$HOME/krita/build  \\"; \
    echo "    --ndk-path=/opt/android-sdk/ndk-bundle  \\"; \
    echo "    --sdk-path=/opt/android-sdk \\"; \
    echo "    --api-level=21 --android-abi=\${ANDROID_ABI}"; \ 
    ) > $HOME/build.sh && \
    chmod +x $HOME/build.sh

CMD [ "/bin/bash" ]

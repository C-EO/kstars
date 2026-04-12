#!/usr/bin/env bash
set -euo pipefail

ln -snf "/usr/share/zoneinfo/$TZ" /etc/localtime
echo "$TZ" > /etc/timezone

mkdir -p "$SRC_DIR" "$BUILD_DIR"

cd "$SRC_DIR"
git clone --branch "$BRANCH" --depth 1 "$REPO" "$PACKAGE"

cd "$SRC_DIR/$PACKAGE"
patch -p1 < /tmp/phd2-cmake-minimum.patch

# Install base packages for PHD2
case "$TARGET" in
    opensuse)
        zypper --non-interactive install \
            git \
            cmake \
            pkg-config \
            wxGTK3-3_2-devel \
            libnova-devel \
            gettext \
            zlib-ng-compat-devel \
            libX11-devel \
            opencv-devel \
            libcurl-devel \
            eigen3-devel
        ;;
    ubuntu)
        apt-get update

        if [ "$(lsb_release -rs | cut -d. -f1)" -le 22 ]; then
            # Ubuntu <= 22
            apt-get install -y \
                libwxgtk3.0-gtk3-dev \
                wx3.0-i18n
        else
            apt-get install -y \
                libwxgtk3.2-dev \
                wx3.2-i18n
        fi

        apt-get -y install \
            build-essential \
            git \
            cmake \
            pkg-config \
            wx-common \
            libnova-dev \
            gettext \
            zlib1g-dev \
            libopencv-dev \
            libcurl4-gnutls-dev \
            libeigen3-dev
        ;;
    *)
        echo "Unsupported TARGET: $TARGET" >&2
        exit 1
        ;;
esac

cd "$BUILD_DIR"

cmake -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_BUILD_TYPE=Debug \
      -DUSE_SYSTEM_LIBINDI=Yes \
      "$SRC_DIR/$PACKAGE"

make -j"${THREADS}" install

rm -rf /var/lib/apt/lists/* 2>/dev/null || true
rm -rf "$SRC_DIR" "$BUILD_DIR"

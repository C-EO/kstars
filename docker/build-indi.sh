#!/usr/bin/env bash
set -euo pipefail

# Install dependencies for INDI
case "$TARGET" in
    opensuse)
        zypper --non-interactive install cmake gcc-c++ git
        zypper --non-interactive install \
            dkms \
            fxload \
            libev-devel \
            gsl-devel \
            libraw-devel \
            libusb-devel \
            libjpeg-devel \
            libnova-devel \
            libtiff-devel \
            fftw3-devel \
            cfitsio-devel \
            libgphoto2-devel \
            libusb-1_0-devel \
            libdc1394-devel \
            libcurl-devel \
            libtheora-devel \
            libftdi1-devel
        ;;
    kde)
        zypper --non-interactive install cmake gcc-c++ git
        zypper --non-interactive remove libevent-devel
        zypper --non-interactive install \
            dkms \
            fxload \
            libev-devel \
            gsl-devel \
            libraw-devel \
            libusb-devel \
            libjpeg-devel \
            libnova-devel \
            libtiff-devel \
            fftw3-devel \
            cfitsio-devel \
            libgphoto2-devel \
            libusb-1_0-devel \
            libdc1394-devel \
            libcurl-devel \
            libtheora-devel \
            libftdi1-devel
        ;;
    ubuntu)
        apt-get update
        apt-get install -y \
            git \
            cdbs \
            dkms \
            cmake \
            fxload \
            libev-dev \
            libgps-dev \
            libgsl-dev \
            libraw-dev \
            libusb-dev \
            zlib1g-dev \
            libftdi-dev \
            libgsl0-dev \
            libjpeg-dev \
            libkrb5-dev \
            libnova-dev \
            libtiff-dev \
            libfftw3-dev \
            librtlsdr-dev \
            libcfitsio-dev \
            libgphoto2-dev \
            build-essential \
            libusb-1.0-0-dev \
            libdc1394-dev \
            libboost-regex-dev \
            libcurl4-gnutls-dev \
            libtheora-dev
        ;;
    *)
        echo "Unsupported TARGET: $TARGET" >&2
        exit 1
        ;;
esac

mkdir -p "$SRC_DIR" "$BUILD_DIR"

cd "$SRC_DIR"
git clone --branch "$BRANCH" --depth 1 "$REPO" "$PACKAGE"

cd "$BUILD_DIR"
cmake -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_BUILD_TYPE=Debug \
      "$SRC_DIR/$PACKAGE"

make -j"${THREADS}" install

rm -rf /var/lib/apt/lists/* 2>/dev/null || true
rm -rf "$SRC_DIR" "$BUILD_DIR"


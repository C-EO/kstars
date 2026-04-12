#!/usr/bin/env bash
set -euo pipefail

: "${MIN_VERSION:=4.6.0}"

version_ge() {
    # Returns success if $1 >= $2
    dpkg --compare-versions "$1" ge "$2"
}

has_opencv_ge_min_version() {
    local v=""
    local header=""
    local major=""
    local minor=""
    local revision=""

    # 1) Check opencv_version binary
    if command -v opencv_version >/dev/null 2>&1; then
        v="$(opencv_version 2>/dev/null || true)"
        v="${v%% *}"
        if [ -n "$v" ] && version_ge "$v" "$MIN_VERSION"; then
            echo "$v"
            return 0
        fi
    fi

    # 2) Check version headers
    for header in \
        /usr/include/opencv4/opencv2/core/version.hpp \
        /usr/local/include/opencv4/opencv2/core/version.hpp \
        /usr/include/opencv2/core/version.hpp \
        /usr/local/include/opencv2/core/version.hpp
    do
        if [ -f "$header" ]; then
            major="$(awk '/#define CV_VERSION_MAJOR / {print $3}' "$header" | head -n1)"
            minor="$(awk '/#define CV_VERSION_MINOR / {print $3}' "$header" | head -n1)"
            revision="$(awk '/#define CV_VERSION_REVISION / {print $3}' "$header" | head -n1)"

            if [ -n "$major" ] && [ -n "$minor" ] && [ -n "$revision" ]; then
                v="${major}.${minor}.${revision}"
                if version_ge "$v" "$MIN_VERSION"; then
                    echo "$v"
                    return 0
                fi
            fi
        fi
    done

    return 1
}

install_opencv_if_needed() {
    local apt_version=""
    local detected_version=""

    if detected_version="$(has_opencv_ge_min_version)"; then
        echo "OpenCV version $detected_version found"
        return 0
    fi

    apt-get update

    apt_version="$(apt-cache policy libopencv-dev | awk '/Candidate:/ {print $2}')"

    if [ -n "$apt_version" ] \
       && [ "$apt_version" != "(none)" ] \
       && version_ge "$apt_version" "$MIN_VERSION"; then
        apt-get install -y libopencv-dev
        return 0
    fi

    # OpenCV must be built manually here.
    mkdir -p "$SRC_DIR" "$BUILD_DIR"

    cd "$SRC_DIR"
    git clone --branch "$BRANCH" --depth 1 "$REPO" "$PACKAGE"

    cd "$BUILD_DIR"
    cmake -DCMAKE_INSTALL_PREFIX=/usr "$SRC_DIR/$PACKAGE"
    make -j"${THREADS}" install

    cd "$HOME"
    rm -rf "$SRC_DIR" "$BUILD_DIR"

    return 1
}

apt-get update
apt-get install -y git cmake build-essential

install_opencv_if_needed

rm -rf /var/lib/apt/lists/*


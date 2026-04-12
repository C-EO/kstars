#!/usr/bin/env bash
set -euo pipefail

: "${HOME:=/home/sterne-jaeger}"
: "${BIN_DIR:=$HOME/bin}"
: "${APT_PACKAGES_GSC:=gsc gsc-data}"

has_gsc_installed() {
    local pkg=""

    for pkg in $APT_PACKAGES_GSC; do
        if ! dpkg -s "$pkg" >/dev/null 2>&1; then
            return 1
        fi
    done

    return 0
}

can_install_gsc_from_apt() {
    local pkg=""
    local candidate=""

    apt-get update

    for pkg in $APT_PACKAGES_GSC; do
        candidate="$(apt-cache policy "$pkg" | awk '/Candidate:/ {print $2}')"
        if [ -z "$candidate" ] || [ "$candidate" = "(none)" ]; then
            return 1
        fi
    done

    return 0
}

install_gsc_if_needed() {
    if has_gsc_installed; then
        echo "GSC packages found"
        return 0
    fi

    if can_install_gsc_from_apt; then
        if apt-get install -y --no-install-recommends $APT_PACKAGES_GSC; then
            echo "GSC packages installed from apt"
            rm -rf /var/lib/apt/lists/*
            return 0
        fi

        echo "GSC packages found but not installable from apt, calling build-gsc.sh"
    else
        echo "No suitable apt packages found for GSC, calling build-gsc.sh"
    fi

    "$BIN_DIR/build-gsc.sh"
}

install_gsc_if_needed

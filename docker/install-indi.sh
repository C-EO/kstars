#!/usr/bin/env bash
set -euo pipefail

: "${HOME:=/home/sterne-jaeger}"
: "${BIN_DIR:=$HOME/bin}"
: "${APT_PACKAGES_INDI:=libindi1 libindi-dev libindi-data indi-bin}"

has_indi_installed() {
    local pkg=""

    for pkg in $APT_PACKAGES_INDI; do
        if ! dpkg -s "$pkg" >/dev/null 2>&1; then
            return 1
        fi
    done

    return 0
}

can_install_indi_from_apt() {
    local pkg=""
    local candidate=""

    apt-get update

    for pkg in $APT_PACKAGES_INDI; do
        candidate="$(apt-cache policy "$pkg" | awk '/Candidate:/ {print $2}')"
        if [ -z "$candidate" ] || [ "$candidate" = "(none)" ]; then
            return 1
        fi
    done

    return 0
}

install_indi_if_needed() {
    if has_indi_installed; then
        echo "INDI packages found"
        return 0
    fi

    if can_install_indi_from_apt; then
        if apt-get install -y --no-install-recommends $APT_PACKAGES_INDI; then
            echo "INDI packages installed from apt"
            return 0
        fi

        echo "INDI packages found but not installable from apt, building from source"
    else
        echo "No suitable apt packages found, building INDI from source"
    fi

    "$BIN_DIR/build-indi.sh"
}

install_indi_if_needed

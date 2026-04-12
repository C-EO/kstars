#!/usr/bin/env bash
set -euo pipefail

: "${HOME:=/home/sterne-jaeger}"
: "${APT_PACKAGE_PHD2:=phd2}"
: "${BIN_DIR:=$HOME/bin}"

has_phd2_installed() {
    if command -v phd2 >/dev/null 2>&1; then
        return 0
    fi

    if [ -x /usr/bin/phd2 ] || [ -x /usr/local/bin/phd2 ]; then
        return 0
    fi

    return 1
}

can_install_phd2_from_apt() {
    local candidate=""

    apt-get update
    candidate="$(apt-cache policy "$APT_PACKAGE_PHD2" | awk '/Candidate:/ {print $2}')"

    [ -n "$candidate" ] && [ "$candidate" != "(none)" ]
}

install_phd2_if_needed() {
    if has_phd2_installed; then
        echo "PHD2 found"
        return 0
    fi

    if can_install_phd2_from_apt; then
        if apt-get install -y --no-install-recommends "$APT_PACKAGE_PHD2"; then
            echo "PHD2 installed from apt"
            return 0
        fi

        echo "PHD2 package found but not installable from apt, building from source"
    else
        echo "No suitable apt package found, building PHD2 from source"
    fi

    "$BIN_DIR/build-phd2.sh"
}

install_phd2_if_needed

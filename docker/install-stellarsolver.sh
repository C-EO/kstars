#!/usr/bin/env bash
set -euo pipefail

: "${HOME:=/home/sterne-jaeger}"
: "${PACKAGE:=stellarsolver}"
: "${APT_PACKAGE_STELLARSOLVER:=libstellarsolver-dev}"
: "${BIN_DIR:=$HOME/bin}"

has_stellarsolver_installed() {
    # 1) Check common header locations
    if [ -f /usr/include/stellarsolver/stellarsolver.h ] \
       || [ -f /usr/local/include/stellarsolver/stellarsolver.h ] \
       || [ -f /usr/include/libstellarsolver/stellarsolver.h ] \
       || [ -f /usr/local/include/libstellarsolver/stellarsolver.h ]; then
        return 0
    fi

    # 2) Check common library locations
    if compgen -G "/usr/lib*/libstellarsolver.so*" >/dev/null 2>&1 \
       || compgen -G "/usr/local/lib*/libstellarsolver.so*" >/dev/null 2>&1; then
        return 0
    fi

    return 1
}

install_stellarsolver_if_needed() {
    if has_stellarsolver_installed; then
        echo "Stellarsolver found"
        return 0
    fi

    apt-get update

    if apt-cache policy "$APT_PACKAGE_STELLARSOLVER" | awk '/Candidate:/ { exit ($2 == "(none)") }'; then
        apt-get install -y --no-install-recommends "$APT_PACKAGE_STELLARSOLVER"
        echo "Stellarsolver installed from apt"
        return 0
    fi

    echo "No suitable apt package found, building Stellarsolver from source"

    "$BIN_DIR/build-stellarsolver.sh"
}

# execute
install_stellarsolver_if_needed

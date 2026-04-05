ARG BUILD_IMAGE=ubuntu:24.04
# ARG BUILD_IMAGE=ubuntu:25.10
FROM ${BUILD_IMAGE}

ARG QT_VERSION=6

ENV HOME=/root
ENV QT_VERSION=${QT_VERSION}
ENV TZ=Greenwich
ENV QT_QPA_PLATFORMTHEME=qt6ct
ENV CCACHE_DIR=/var/ccache

RUN mkdir -p "$HOME/bin"
COPY build.sh "$HOME/bin/build.sh"

RUN chmod +x "$HOME/bin/build.sh" \
 && "$HOME/bin/build.sh"

CMD /bin/bash

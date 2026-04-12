ARG BUILD_IMAGE=ubuntu:24.04
# ARG BUILD_IMAGE=ubuntu:25.10
FROM ${BUILD_IMAGE}

ARG QT_VERSION=6
ARG TARGET=ubuntu

ENV HOME=/root
ENV TARGET=${TARGET}
ENV THREADS=10
ENV SRC_DIR=$HOME/git

RUN mkdir -p "$HOME/bin"

# add additional repository
RUN apt update && \
    apt -y install --no-install-recommends \
           software-properties-common \
	   gpg-agent \
	   git && \
    add-apt-repository -y ppa:mutlaqja/ppa


# #######################
# INDI installation
# #######################
ARG REPO_INDI=https://github.com/indilib/indi.git
ARG BRANCH_INDI=master

ENV PACKAGE=indi
ENV BUILD_DIR=$HOME/build/$PACKAGE
ENV REPO=${REPO_INDI}
ENV BRANCH=${BRANCH_INDI}

COPY --chmod=+x build-indi.sh "$HOME/bin/build-indi.sh"
COPY --chmod=+x install-indi.sh "$HOME/bin/install-indi.sh"

RUN "$HOME/bin/install-indi.sh"

# #######################
# StellarSolver installation
# #######################
ARG REPO_STELLARSOLVER=https://github.com/sterne-jaeger/stellarsolver.git
ARG BRANCH_STELLARSOLVER=master

ENV PACKAGE=stellarsolver
ENV BUILD_DIR=$HOME/build/$PACKAGE
ENV REPO=${REPO_STELLARSOLVER}
ENV BRANCH=${BRANCH_STELLARSOLVER}

COPY --chmod=+x build-stellarsolver.sh "$HOME/bin/build-stellarsolver.sh"
COPY --chmod=+x install-stellarsolver.sh "$HOME/bin/install-stellarsolver.sh"

RUN "$HOME/bin/install-stellarsolver.sh"

# #######################
# PHD2 installation
# #######################
ARG REPO_PHD2=https://github.com/OpenPHDGuiding/phd2.git
ARG BRANCH_PHD2=master

ENV PACKAGE=phd2
ENV BUILD_DIR=$HOME/build/$PACKAGE
ENV REPO=${REPO_PHD2}
ENV BRANCH=${BRANCH_PHD2}
ENV TZ=Greenwich

COPY --chmod=+x build-phd2.sh "$HOME/bin/build-phd2.sh"
COPY --chmod=+x install-phd2.sh "$HOME/bin/install-phd2.sh"
COPY phd2-cmake-minimum.patch /tmp/phd2-cmake-minimum.patch

RUN add-apt-repository -y ppa:pch/phd2 && apt update

RUN "$HOME/bin/install-phd2.sh"

#######################
# install openCV
#######################
ARG REPO_OPENCV=https://github.com/opencv/opencv.git
ARG BRANCH_OPENCV=master

COPY --chmod=+x install-opencv.sh $HOME/bin/install-opencv.sh

ENV PACKAGE=opencv
ENV BUILD_DIR=$HOME/build/$PACKAGE
ENV REPO=$REPO_OPENCV
ENV BRANCH=$BRANCH_OPENCV

RUN "$HOME/bin/install-opencv.sh"

#######################
# install GSC
#######################
COPY --chmod=+x build-gsc.sh "$HOME/bin/build-gsc.sh"
COPY --chmod=+x install-gsc.sh "$HOME/bin/install-gsc.sh"

ENV PACKAGE=gsc
ENV BUILD_DIR=$HOME/build/$PACKAGE

RUN "$HOME/bin/install-gsc.sh"

#######################
# prepare image
#######################
ENV QT_QPA_PLATFORMTHEME=qt6ct
ENV CCACHE_DIR=/var/ccache

RUN mkdir -p "$HOME/bin"
COPY --chmod=+x prepare-kstars-ci.sh "$HOME/bin/prepare-kstars-ci.sh"

RUN "$HOME/bin/prepare-kstars-ci.sh"

CMD /bin/bash

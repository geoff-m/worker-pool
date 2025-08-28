# syntax=docker/dockerfile:1

FROM debian:12 AS builder
ARG REPO_ROOT=/usr/local/worker-pool
ARG CMAKE_BUILD_TYPE
WORKDIR $REPO_ROOT

# Need to have netcat to detect apt proxy.
RUN apt-get update && apt-get install -y netcat-traditional
COPY docker/apt-proxy/detect-apt-proxy.sh /usr/local/bin/
COPY docker/apt-proxy/99proxy.conf /etc/apt/apt.conf.d/99proxy.conf

RUN apt-get update && apt-get install -y \
    wget \
    gnupg
RUN echo  "deb http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm main" >> /etc/apt/source.list \
  && echo "deb-src http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm main" >> /etc/apt/sources.list
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
RUN apt-get update && apt-get install -y \
    git \
    cmake \
    ninja-build \
    clang

# Copy code
COPY CMakeLists.txt CMakeLists.txt
COPY cmake cmake
COPY include include
COPY thirdparty thirdparty
COPY test test

# Build
WORKDIR $REPO_ROOT/build/
RUN cmake \
  -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER=clang \
  -G Ninja \
  "$REPO_ROOT" && cmake --build . -j$(nproc)

# Run unit tests
RUN ctest --parallel $(nproc) -V


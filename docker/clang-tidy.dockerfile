# syntax=docker/dockerfile:1

FROM debian:13 AS builder
ARG REPO_ROOT=/tmp/worker-pool/
WORKDIR $REPO_ROOT

RUN apt-get update && apt-get install -y \
    curl \
    gnupg

# Work around https://github.com/llvm/llvm-project/issues/153385
RUN if [ -f /usr/share/apt/default-sequoia.config ]; then \
            sed -i 's/\(sha1\.second_preimage_resistance =\).*/\1 2027-01-01/' /usr/share/apt/default-sequoia.config; \
fi

RUN curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /etc/apt/keyrings/llvm-snapshot.gpg \
&& chmod a+r /etc/apt/keyrings/llvm-snapshot.gpg \
&& . /etc/os-release \
&& tee /etc/apt/sources.list.d/llvm.sources <<EOF
Enabled: yes
Types: deb
URIs: http://apt.llvm.org/$VERSION_CODENAME/
Suites: llvm-toolchain-$VERSION_CODENAME
Components: main
Signed-By: /etc/apt/keyrings/llvm-snapshot.gpg
EOF

RUN apt-get update && apt-get install -y \
    git \
    cmake \
    ninja-build \
    clang \
    clang-tidy \
    parallel

# Copy code
COPY CMakeLists.txt CMakeLists.txt
COPY cmake cmake
COPY include include
COPY src src
COPY test test
COPY .clang-tidy .clang-tidy

RUN mkdir build

# Rebuild with deadlock detection on
RUN cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_COMPILER=clang \
  -DWORKER_POOL_TEST=ON \
  -DWORKER_POOL_LOGGING=ON \
  -DWORKER_POOL_DEADLOCK_DETECTION=ON \
  -G Ninja \
  -S . \
  -B build \
   && cmake --build build -j$(nproc)

# Verify clang-tidy configuration
RUN clang-tidy -p=build --quiet --config-file=.clang-tidy --verify-config

# Run clang-tidy
RUN find src include test -regex '.*\.\(cpp\|h\)' |\
      parallel -j $(nproc) \
      clang-tidy -p=build --quiet --config-file=.clang-tidy

FROM ubuntu:noble
RUN apt-get update && apt-get install -y \
  locales curl build-essential cmake meson clang clangd llvm lldb lld mold python3-lldb ninja-build \
  python3 autoconf automake lsb-release software-properties-common gnupg2 apt-transport-https catch2 git pkg-config
RUN update-alternatives --set cc /usr/bin/clang
RUN update-alternatives --set c++ /usr/bin/clang++

USER ubuntu
WORKDIR /home/ubuntu
CMD /bin/bash

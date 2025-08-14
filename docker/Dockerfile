FROM ubuntu:noble
RUN apt-get update && apt-get install -y \
  curl build-essential cmake meson clang clangd llvm lldb lld mold python3-lldb ninja-build \
  sudo 7zip python3 autoconf automake catch2 git pkg-config zstd valgrind locales locales-all \
  libglib2.0-dev libexpat1-dev libpcre3-dev libhwy-dev libpng-dev libjpeg-turbo8-dev liblzma-dev \
  libheif-dev libjxl-dev libpoppler-glib-dev librsvg2-dev libdwarf-dev liblucene++-dev libboost-dev
RUN update-alternatives --set cc /usr/bin/clang
RUN update-alternatives --set c++ /usr/bin/clang++
RUN sh -c "$(curl --location https://taskfile.dev/install.sh)" -- -d -b /usr/local/bin
RUN curl -fsSL https://deno.land/install.sh | sh
RUN mv /root/.deno/bin/deno /usr/local/bin/

ENV LC_ALL en_US.UTF-8
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US.UTF-8

USER ubuntu
WORKDIR /home/ubuntu
CMD /bin/bash

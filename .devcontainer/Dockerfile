FROM seajackal/hydrolib:latest

ARG USERNAME=hydrolib
ARG UID=1000
ARG GID=$UID

RUN useradd --shell /bin/bash --create-home ${USERNAME}

RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add - \
    && echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-19 main" \
    | sudo tee /etc/apt/sources.list.d/llvm.list \
    && apt update -q \
    && apt upgrade -q -y \
    && apt install -y --no-install-recommends \
    openocd \
    gdb-multiarch \
    python3-pip \
    && apt clean \
    && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

RUN pip install compdb==0.2.0 \
    && pip install 'pyrenode3[all] @ git+https://github.com/antmicro/pyrenode3.git' \
    && pip install pyserial

ENV GCC_ARM_VERSION=14.2.1-1.1
RUN wget -q https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${GCC_ARM_VERSION}/xpack-arm-none-eabi-gcc-${GCC_ARM_VERSION}-linux-x64.tar.gz \
    && tar -xf xpack-arm-none-eabi-gcc-${GCC_ARM_VERSION}-linux-x64.tar.gz -C /opt \
    && rm xpack-arm-none-eabi-gcc-${GCC_ARM_VERSION}-linux-x64.tar.gz \
    && cp -s /opt/xpack-arm-none-eabi-gcc-${GCC_ARM_VERSION}/bin/* /usr/local/bin/ \
    && ln -s /opt/xpack-arm-none-eabi-gcc-${GCC_ARM_VERSION}/arm-none-eabi/include /usr/include/newlib

RUN echo "Скачивание стабильного Renode с GitHub..." \
    && apt-get update && apt-get install -y curl tar \
    && mkdir -p /opt/renode \
    && curl -fSL "https://github.com" -o /tmp/renode.tar.gz \
    && tar -xf /tmp/renode.tar.gz -C /opt/renode --strip-components=1 \
    && chown -R ${USERNAME}:${USERNAME} /opt/renode \
    && ln -s /opt/renode/renode /usr/local/bin/renode \
    && rm /tmp/renode.tar.gz \
    && rm -rf /var/lib/apt/lists/* \
    && echo "Установка Renode успешно завершена!"

ENV PYRENODE_PATH=/opt/renode/renode

USER ${USERNAME}
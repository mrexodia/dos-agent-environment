FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    qemu-system-x86 qemu-utils \
    mtools parted dosfstools \
    build-essential ca-certificates gettext curl unzip git nasm \
    python3 python3-pexpect python3-pytest python3-pil \
    netcat-openbsd socat \
    && rm -rf /var/lib/apt/lists/*

ARG MS_SYS_REF=318df77ec01fbdc4084263fa9a828f662ce3c685
RUN git clone https://github.com/pbatard/ms-sys /tmp/ms-sys \
    && cd /tmp/ms-sys \
    && git checkout "$MS_SYS_REF" \
    && make \
    && make install \
    && rm -rf /tmp/ms-sys

WORKDIR /workspace

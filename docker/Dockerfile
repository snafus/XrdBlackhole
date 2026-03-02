# ---------- builder ----------
FROM almalinux:9

ENV XROOTD_VERSION=5.9.1-1.el9

RUN dnf -y update && dnf -y install dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf -y install epel-release && \
    dnf -y install \
      gcc gcc-c++ make cmake git \
      openssl-devel zlib-devel curl-devel libuuid-devel rpm-build rpmdevtools

RUN curl -L https://xrootd.web.cern.ch/xrootd.repo -o /etc/yum.repos.d/xrootd.repo && \
      dnf -y install \
        xrootd-devel-${XROOTD_VERSION} \
        xrootd-server-${XROOTD_VERSION} \
        xrootd-server-devel-${XROOTD_VERSION} \
        xrootd-private-devel-${XROOTD_VERSION} \
        xrootd-client-devel-${XROOTD_VERSION} \
        json-c-devel && \
    dnf clean all

WORKDIR /src
COPY . /src

RUN mkdir build && cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
    cmake --build build && \
    echo "=== Built libraries ===" && ls build/src/libXrdBlackhole*.so

RUN mkdir -p /usr/lib64/xrootd && \
    ln -s /src/build/src/libXrdBlackhole-5.so       /usr/lib64/xrootd/libXrdBlackhole-5.so && \
    ln -s /src/build/src/libXrdBlackholeXattr-5.so   /usr/lib64/xrootd/libXrdBlackholeXattr-5.so && \
    ln -s /src/build/src/libXrdBlackholeMetrics-5.so /usr/lib64/xrootd/libXrdBlackholeMetrics-5.so

RUN groupadd -r xrootd && \
    useradd -r -g xrootd -d /var/lib/xrootd -s /sbin/nologin xrootd && \
    mkdir -p /var/log/xrootd /var/run/xrootd && \
    chown xrootd:xrootd /var/log/xrootd /var/run/xrootd

COPY config/xrootd-blackhole.cfg /etc/xrootd/xrootd-blackhole.cfg

EXPOSE 1094

USER xrootd:xrootd

CMD ["/usr/bin/xrootd", "-f", "-c", "/etc/xrootd/xrootd-blackhole.cfg", "-l", "/dev/stderr"]

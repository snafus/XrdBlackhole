# =============================================================================
# Stage 1: Build the RPM
# =============================================================================
FROM almalinux:9 AS rpm-builder

ENV XROOTD_VERSION=5.9.1-1.el9

RUN dnf -y update && dnf -y install dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf -y install epel-release && \
    dnf -y install \
      gcc gcc-c++ make cmake git \
      openssl-devel zlib-devel curl-devel libuuid-devel \
      rpm-build rpmdevtools

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

RUN rpmdev-setuptree && \
    RAW=$(./genversion.sh --print-only . 2>/dev/null) && \
    VERSION=${RAW#v} && \
    VERSION=$(echo "$VERSION" | sed 's/-/./g') && \
    RELEASE=1 && \
    echo "=== Building RPM version ${VERSION}-${RELEASE} ===" && \
    git archive --prefix=xrootd-blackhole/ --format=tar HEAD \
        | gzip -9 > ~/rpmbuild/SOURCES/xrootd-blackhole.tar.gz && \
    sed "s/__VERSION__/${VERSION}/g; s/__RELEASE__/${RELEASE}/g" \
        packaging/rhel/xrootd-blackhole.spec.in \
        > ~/rpmbuild/SPECS/xrootd-blackhole.spec && \
    rpmbuild -bb ~/rpmbuild/SPECS/xrootd-blackhole.spec && \
    echo "=== Built RPMs ===" && find ~/rpmbuild/RPMS -name '*.rpm'

# =============================================================================
# Stage 2: Runtime image — install from RPM into a clean OS base
# =============================================================================
FROM almalinux:9

ENV XROOTD_VERSION=5.9.1-1.el9

RUN dnf -y update && dnf -y install dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf -y install epel-release

RUN curl -L https://xrootd.web.cern.ch/xrootd.repo -o /etc/yum.repos.d/xrootd.repo && \
    dnf -y install \
      xrootd-server-${XROOTD_VERSION} && \
    dnf clean all

# Install the plugin RPM built in stage 1; dnf resolves any missing deps.
COPY --from=rpm-builder /root/rpmbuild/RPMS/ /tmp/rpms/
RUN dnf -y install /tmp/rpms/*/*.rpm && \
    rm -rf /tmp/rpms

RUN groupadd -r xrootd && \
    useradd -r -g xrootd -d /var/lib/xrootd -s /sbin/nologin xrootd && \
    mkdir -p /var/log/xrootd /var/run/xrootd && \
    chown xrootd:xrootd /var/log/xrootd /var/run/xrootd

COPY config/xrootd-blackhole.cfg /etc/xrootd/xrootd-blackhole.cfg

EXPOSE 1094

USER xrootd:xrootd

CMD ["/usr/bin/xrootd", "-f", "-c", "/etc/xrootd/xrootd-blackhole.cfg", "-l", "/dev/stderr"]

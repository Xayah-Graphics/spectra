# syntax=docker/dockerfile:1
# hadolint global ignore=DL3007

FROM archlinux:latest AS build

SHELL ["/bin/bash", "-euo", "pipefail", "-c"]

RUN --mount=type=cache,target=/var/cache/pacman/pkg,sharing=locked \
    pacman -Syu --noconfirm --needed \
        base-devel \
        cmake \
        curl \
        git \
        ninja \
        python \
        spirv-tools \
        vulkan-headers \
        vulkan-icd-loader

ARG SLANG_RELEASE=latest
RUN release_endpoint="latest"; \
    if [ "${SLANG_RELEASE}" != "latest" ]; then release_endpoint="tags/${SLANG_RELEASE}"; fi; \
    release="$(curl --fail --silent --show-error --location \
        --header "X-GitHub-Api-Version: 2022-11-28" \
        "https://api.github.com/repos/shader-slang/slang/releases/${release_endpoint}")"; \
    printf '%s' "${release}" | python -c 'import json, sys; release = json.load(sys.stdin); asset = next(asset for asset in release["assets"] if asset["name"].endswith("-linux-x86_64.tar.gz")); print("{} {}".format(asset["browser_download_url"], asset["digest"].removeprefix("sha256:")))' > /tmp/slang-asset; \
    read -r asset_url asset_digest < /tmp/slang-asset; \
    curl --fail --silent --show-error --location --retry 5 --output /tmp/slang.tar.gz "${asset_url}"; \
    printf '%s  %s\n' "${asset_digest}" /tmp/slang.tar.gz | sha256sum --check --status; \
    mkdir --parents /opt/slang; \
    tar --extract --gzip --file /tmp/slang.tar.gz --directory /opt/slang

ENV PATH="/opt/slang/bin:${PATH}"

WORKDIR /src
COPY --link . .

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=gcc \
        -DCMAKE_CXX_COMPILER=g++ \
        -DSPECTRA_BUILD_UI=OFF \
    && cmake --build build --parallel 30 \
    && cmake --install build --prefix /opt/spectra --component Spectra


FROM archlinux:latest AS runtime

RUN --mount=type=cache,target=/var/cache/pacman/pkg,sharing=locked \
    pacman -Syu --noconfirm --needed gcc-libs vulkan-icd-loader \
    && groupadd --gid 10001 spectra \
    && useradd --uid 10001 --gid 10001 --home-dir /opt/spectra --shell /usr/bin/nologin spectra

COPY --from=build --chown=spectra:spectra --link /opt/spectra/ /opt/spectra/
COPY --from=build --link /src/LICENSE /usr/share/licenses/spectra/LICENSE

USER 10001:10001
ENV HOME=/opt/spectra
WORKDIR /opt/spectra

ENTRYPOINT ["/opt/spectra/spectra"]

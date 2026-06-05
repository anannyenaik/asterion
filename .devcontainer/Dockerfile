FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        libclang-rt-18-dev \
        cmake \
        git \
        ninja-build \
        python3 \
        python3-dev \
        python3-pip \
        python3-pytest \
        python3-venv \
        sudo \
    && rm -rf /var/lib/apt/lists/*

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=1000

RUN if getent group "${USER_GID}" >/dev/null; then \
        existing_group="$(getent group "${USER_GID}" | cut -d: -f1)"; \
        [ "${existing_group}" = "${USERNAME}" ] || groupmod --new-name "${USERNAME}" "${existing_group}"; \
    else \
        groupadd --gid "${USER_GID}" "${USERNAME}"; \
    fi \
    && if getent passwd "${USER_UID}" >/dev/null; then \
        existing_user="$(getent passwd "${USER_UID}" | cut -d: -f1)"; \
        [ "${existing_user}" = "${USERNAME}" ] \
            || usermod --login "${USERNAME}" --home "/home/${USERNAME}" --move-home "${existing_user}"; \
    else \
        useradd --uid "${USER_UID}" --gid "${USER_GID}" -m -s /bin/bash "${USERNAME}"; \
    fi \
    && usermod --gid "${USERNAME}" "${USERNAME}" \
    && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > "/etc/sudoers.d/${USERNAME}" \
    && chmod 0440 "/etc/sudoers.d/${USERNAME}"

USER ${USERNAME}
WORKDIR /workspaces/Asterion

CMD ["/bin/bash"]

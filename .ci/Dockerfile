FROM ubuntu:24.10

ENV DEBIAN_FRONTEND=noninteractive

ENV PATH="/home/user/bin:${PATH}"

# Taking into account layer structure, everything should be done within one layer.
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y clang-19 clang-tidy-19 clang-format-19 git libdrm-dev blueprint-tools libgtest-dev clang \
    llvm make python3 wget sudo rsync lld pkg-config ninja-build meson \
    python3-mako python3-jinja2 python3-ply python3-yaml

ENV RUN_USER user
ENV RUN_UID 1000

ENV USER_HOME /home/${RUN_USER}

RUN mkdir -pv ${USER_HOME}

# Delete default user
RUN userdel -r ubuntu

# Create new user
RUN adduser \
    --gecos 'Build User' \
    --shell '/usr/bin/bash' \
    --uid ${RUN_UID} \
    --disabled-login \
    --disabled-password ${RUN_USER} \
    && adduser ${RUN_USER} sudo

RUN chown -R ${RUN_USER}:${RUN_USER} ${USER_HOME} && chmod -R 775 ${USER_HOME}

# Ensure sudo group users are not
# asked for a password when using
# sudo command by ammending sudoers file
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> \
/etc/sudoers

# Pass control to a newly created user
USER ${RUN_USER}

# Install aospless package (produced by GloDroid/aospext)
RUN wget -P ${USER_HOME} https://gitlab.freedesktop.org/-/project/5/uploads/97f99b51143107ba02a51cf1c0ddb542/aospless_drm_hwcomposer_arm64.tar.xz && \
    cd ${USER_HOME} && \
    sha256sum aospless_drm_hwcomposer_arm64.tar.xz && \
    (echo dae29adb121f51e59c95fb7b29e0f7aed5b2983d10c7f1d5f1b9fd551c4bbb47 aospless_drm_hwcomposer_arm64.tar.xz | sha256sum --check) && \
    tar xf aospless_drm_hwcomposer_arm64.tar.xz && ln -s ../drm_hwcomposer/ ${USER_HOME}/aospless/src

# Create project path
RUN mkdir -pv ${USER_HOME}/drm_hwcomposer
WORKDIR ${USER_HOME}/drm_hwcomposer

RUN git config --global user.name "FIRST_NAME LAST_NAME" && git config --global user.email "MY_NAME@example.com"

CMD [ "/bin/bash" ]

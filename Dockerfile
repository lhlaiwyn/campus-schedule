# 单阶段镜像：直接装好构建依赖、编译、再运行。
#
# 之所以不做多阶段精简，是因为运行阶段所需的运行库包名随发行版变化，
# 没有实际构建过一次就写出来很容易在别人机器上失败。先保证可复现，
# 体积优化记录在 README 的「后续优化」里。
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Asia/Shanghai

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        libcpp-httplib-dev \
        libspdlog-dev \
        nlohmann-json3-dev \
        libmysqlclient-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tests ./tests

# 构建时就跑一遍单元测试：镜像构建失败好过带病上线
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCAMPUS_BUILD_TESTS=ON \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

ENV CAMPUS_HOST=0.0.0.0 \
    CAMPUS_PORT=8080 \
    CAMPUS_DB_HOST=mysql \
    CAMPUS_DB_PORT=3306 \
    CAMPUS_DB_USER=campus \
    CAMPUS_DB_PASSWORD=campus_dev_2026 \
    CAMPUS_DB_NAME=campus_schedule \
    CAMPUS_PORTAL_ADAPTER=mock

EXPOSE 8080

ENTRYPOINT ["/src/build/campus_server"]


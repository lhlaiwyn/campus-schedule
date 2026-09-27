#!/usr/bin/env bash
# 安装 wrk 压测工具（需要 root 权限）
set -uo pipefail

if command -v wrk >/dev/null 2>&1; then
    echo "wrk 已安装: $(command -v wrk)"
    wrk --version 2>&1 | head -1
    exit 0
fi

echo "正在安装 wrk ..."
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
if apt-get install -y -qq wrk; then
    echo "安装成功"
    wrk --version 2>&1 | head -1
else
    echo "!! 安装失败，源里可能没有 wrk"
    exit 1
fi


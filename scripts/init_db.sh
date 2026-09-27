#!/usr/bin/env bash
# 初始化 MySQL 数据库、账号和表结构
# 需要 root 权限运行： wsl -d Ubuntu -u root -- bash scripts/init_db.sh

set -uo pipefail

DB_NAME="campus_schedule"
DB_USER="campus"
DB_PASS="campus_dev_2026"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCHEMA="${SCRIPT_DIR}/../sql/schema.sql"

echo "==> 检查 MySQL 服务"
if ! mysqladmin ping --silent >/dev/null 2>&1; then
    echo "    MySQL 未运行，尝试启动..."
    systemctl start mysql >/dev/null 2>&1 || service mysql start >/dev/null 2>&1 || true
    sleep 5
fi

if ! mysqladmin ping --silent >/dev/null 2>&1; then
    echo "!! MySQL 启动失败，请手动执行: systemctl status mysql"
    exit 1
fi
echo "    MySQL 运行正常"

echo "==> 创建数据库和账号"
mysql <<SQL
CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`
    CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS '${DB_USER}'@'localhost' IDENTIFIED BY '${DB_PASS}';
ALTER USER '${DB_USER}'@'localhost' IDENTIFIED BY '${DB_PASS}';
GRANT ALL PRIVILEGES ON \`${DB_NAME}\`.* TO '${DB_USER}'@'localhost';
FLUSH PRIVILEGES;
SQL

echo "==> 建表"
if [ ! -f "${SCHEMA}" ]; then
    echo "!! 找不到建表脚本: ${SCHEMA}"
    exit 1
fi
mysql "${DB_NAME}" < "${SCHEMA}"

echo "==> 迁移已有数据库"
# 轻量迁移：检查列是否存在，缺了就补。
# 比「删库重来」对开发更友好，也不会丢掉已有数据。
ensure_column() {
    local table="$1"
    local column="$2"
    local ddl="$3"
    local count
    count=$(mysql -N -B "${DB_NAME}" -e \
        "SELECT COUNT(*) FROM information_schema.columns
         WHERE table_schema='${DB_NAME}' AND table_name='${table}' AND column_name='${column}'")
    if [ "${count}" = "0" ]; then
        echo "  添加 ${table}.${column}"
        mysql "${DB_NAME}" -e "${ddl}"
    else
        echo "  ${table}.${column} 已存在，跳过"
    fi
}

ensure_column courses source \
    "ALTER TABLE courses ADD COLUMN source VARCHAR(16) NOT NULL DEFAULT 'portal' AFTER semester"

echo "==> 当前表"
mysql "${DB_NAME}" -e "SHOW TABLES;"
echo "数据库初始化完成: ${DB_NAME}"

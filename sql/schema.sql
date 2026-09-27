-- 校园课表数据库结构
-- 字符集统一用 utf8mb4：课程名和教师名里可能有生僻字

CREATE TABLE IF NOT EXISTS courses (
    id         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '课程主键',
    name       VARCHAR(128)    NOT NULL                COMMENT '课程名称',
    code       VARCHAR(64)     NOT NULL DEFAULT ''     COMMENT '课程号',
    teacher    VARCHAR(64)     NOT NULL DEFAULT ''     COMMENT '任课教师',
    credits    DECIMAL(4, 1)   NOT NULL DEFAULT 0.0    COMMENT '学分',
    semester   VARCHAR(32)     NOT NULL                COMMENT '学期，如 2026-2027-1',
    source     VARCHAR(16)     NOT NULL DEFAULT 'portal' COMMENT '来源：portal=教务同步 manual=用户手动添加',
    created_at DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_code_semester (code, semester),
    KEY idx_semester (semester)
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_unicode_ci
  COMMENT = '课程';

CREATE TABLE IF NOT EXISTS course_sessions (
    id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '课次主键',
    course_id    BIGINT UNSIGNED NOT NULL                COMMENT '所属课程',
    day_of_week  TINYINT         NOT NULL                COMMENT '1=周一 ... 7=周日',
    start_period TINYINT         NOT NULL                COMMENT '开始节次',
    end_period   TINYINT         NOT NULL                COMMENT '结束节次',
    week_from    SMALLINT        NOT NULL DEFAULT 1      COMMENT '起始周',
    week_to      SMALLINT        NOT NULL DEFAULT 16     COMMENT '结束周',
    week_parity  TINYINT         NOT NULL DEFAULT 0      COMMENT '0=每周 1=单周 2=双周',
    location     VARCHAR(128)    NOT NULL DEFAULT ''     COMMENT '上课地点',
    teacher      VARCHAR(64)     NOT NULL DEFAULT ''     COMMENT '该次课老师（可覆盖课程老师）',
    PRIMARY KEY (id),
    KEY idx_course (course_id),
    KEY idx_day_period (day_of_week, start_period),
    CONSTRAINT fk_session_course FOREIGN KEY (course_id)
        REFERENCES courses (id) ON DELETE CASCADE
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4
  COLLATE = utf8mb4_unicode_ci
  COMMENT = '课次：一门课一周可能上多次';

#include "campus/repository/mysql_course_repository.h"

#include <cstdlib>

#include <spdlog/spdlog.h>

namespace campus {
namespace {

// MySQL 唯一键冲突的客户端错误号。
// 这类错误是「业务冲突」而不是「数据库故障」，需要翻译成 409 让调用方区分。
constexpr unsigned int kDuplicateEntryError = 1062;

// ---------- 结果集取值小工具 ----------

std::string Col(MYSQL_ROW row, unsigned long* lengths, unsigned int index) {
    if (row[index] == nullptr) {
        return {};
    }
    return std::string(row[index], lengths[index]);
}

int ColInt(MYSQL_ROW row, unsigned long* lengths, unsigned int index, int fallback = 0) {
    if (row[index] == nullptr) {
        return fallback;
    }
    try {
        return std::stoi(std::string(row[index], lengths[index]));
    } catch (...) {
        return fallback;
    }
}

std::int64_t ColInt64(MYSQL_ROW row, unsigned long* lengths, unsigned int index) {
    if (row[index] == nullptr) {
        return 0;
    }
    try {
        return std::stoll(std::string(row[index], lengths[index]));
    } catch (...) {
        return 0;
    }
}

double ColDouble(MYSQL_ROW row, unsigned long* lengths, unsigned int index) {
    if (row[index] == nullptr) {
        return 0.0;
    }
    try {
        return std::stod(std::string(row[index], lengths[index]));
    } catch (...) {
        return 0.0;
    }
}

// 课程 + 课次的查询语句只在这里定义一次，改结构时只改这里
constexpr const char* kSelectPrefix = R"SQL(
SELECT c.id, c.name, c.code, c.teacher, c.credits, c.semester,
       s.id, s.day_of_week, s.start_period, s.end_period,
       s.week_from, s.week_to, s.week_parity, s.location, s.teacher,
       c.source
FROM courses c
LEFT JOIN course_sessions s ON s.course_id = c.id
)SQL";

constexpr const char* kSelectSuffix = R"SQL(
ORDER BY c.id ASC, s.day_of_week ASC, s.start_period ASC
)SQL";

// 把 JOIN 出来的连续行聚合成 Course 列表
Result<std::vector<Course>> CollectCourses(MYSQL_RES* result) {
    std::vector<Course> courses;
    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        unsigned long* lengths = mysql_fetch_lengths(result);
        if (lengths == nullptr) {
            break;
        }

        const std::int64_t course_id = ColInt64(row, lengths, 0);
        if (courses.empty() || courses.back().id != course_id) {
            Course course;
            course.id = course_id;
            course.name = Col(row, lengths, 1);
            course.code = Col(row, lengths, 2);
            course.teacher = Col(row, lengths, 3);
            course.credits = ColDouble(row, lengths, 4);
            course.semester = Col(row, lengths, 5);
            // source 故意放在最后一列：这样新增字段不会打乱已有列的索引
            course.source = Col(row, lengths, 15);
            if (course.source.empty()) {
                course.source = kSourcePortal;
            }
            courses.push_back(std::move(course));
        }

        // LEFT JOIN 时若没有课次，第 7 列为 NULL
        if (row[6] != nullptr) {
            CourseSession session;
            session.id = ColInt64(row, lengths, 6);
            session.day_of_week = ColInt(row, lengths, 7);
            session.start_period = ColInt(row, lengths, 8);
            session.end_period = ColInt(row, lengths, 9);
            session.weeks.from = ColInt(row, lengths, 10, 1);
            session.weeks.to = ColInt(row, lengths, 11, 16);
            session.weeks.parity = static_cast<WeekParity>(ColInt(row, lengths, 12, 0));
            session.location = Col(row, lengths, 13);
            session.teacher = Col(row, lengths, 14);
            courses.back().sessions.push_back(std::move(session));
        }
    }
    return Result<std::vector<Course>>::Ok(std::move(courses));
}

Result<std::vector<Course>> RunSelect(MySqlConnection& conn, const std::string& sql) {
    if (mysql_query(conn.handle(), sql.c_str()) != 0) {
        return Result<std::vector<Course>>::Fail(ErrorCode::kDatabaseError,
                                                 mysql_error(conn.handle()));
    }
    MYSQL_RES* result = mysql_store_result(conn.handle());
    if (result == nullptr) {
        return Result<std::vector<Course>>::Fail(ErrorCode::kDatabaseError,
                                                 mysql_error(conn.handle()));
    }
    auto courses = CollectCourses(result);
    mysql_free_result(result);
    return courses;
}

// 插入一门课的所有课次行
bool InsertSessions(MySqlConnection& conn, std::int64_t course_id, const Course& course,
                    std::string& error) {
    for (const auto& s : course.sessions) {
        std::string sql =
            "INSERT INTO course_sessions "
            "(course_id, day_of_week, start_period, end_period, week_from, week_to, week_parity, "
            "location, teacher) VALUES (" +
            std::to_string(course_id) + "," + std::to_string(s.day_of_week) + "," +
            std::to_string(s.start_period) + "," + std::to_string(s.end_period) + "," +
            std::to_string(s.weeks.from) + "," + std::to_string(s.weeks.to) + "," +
            std::to_string(static_cast<int>(s.weeks.parity)) + "," + conn.Quote(s.location) + "," +
            conn.Quote(s.teacher.empty() ? course.teacher : s.teacher) + ")";

        if (mysql_query(conn.handle(), sql.c_str()) != 0) {
            error = mysql_error(conn.handle());
            return false;
        }
    }
    return true;
}

}  // namespace

Result<std::vector<Course>> MySqlCourseRepository::ListBySemester(const std::string& semester) {
    auto conn = pool_.Acquire();
    if (!conn) {
        return Result<std::vector<Course>>::Fail(ErrorCode::kDatabaseError, "数据库连接不可用");
    }

    std::string sql = kSelectPrefix;
    if (!semester.empty()) {
        sql += "WHERE c.semester = " + conn->Quote(semester) + "\n";
    }
    sql += kSelectSuffix;

    return RunSelect(*conn, sql);
}

Result<Course> MySqlCourseRepository::FindById(std::int64_t id) {
    auto conn = pool_.Acquire();
    if (!conn) {
        return Result<Course>::Fail(ErrorCode::kDatabaseError, "数据库连接不可用");
    }

    std::string sql = std::string(kSelectPrefix) + "WHERE c.id = " + std::to_string(id) + "\n" +
                      kSelectSuffix;

    auto courses = RunSelect(*conn, sql);
    if (!courses) {
        return Result<Course>::Fail(courses.error().code, courses.error().message);
    }
    if (courses.value().empty()) {
        return Result<Course>::Fail(ErrorCode::kNotFound, "课程不存在: id=" + std::to_string(id));
    }
    return Result<Course>::Ok(courses.value().front());
}

Result<std::int64_t> MySqlCourseRepository::FindIdByNaturalKey(const Course& course) {
    auto conn = pool_.Acquire();
    if (!conn) {
        return Result<std::int64_t>::Fail(ErrorCode::kDatabaseError, "数据库连接不可用");
    }

    std::string sql = "SELECT id FROM courses WHERE semester = " + conn->Quote(course.semester) +
                      " AND ";
    if (!course.code.empty()) {
        sql += "code = " + conn->Quote(course.code);
    } else {
        sql += "code = '' AND name = " + conn->Quote(course.name) +
               " AND teacher = " + conn->Quote(course.teacher);
    }
    sql += " LIMIT 1";

    if (mysql_query(conn->handle(), sql.c_str()) != 0) {
        return Result<std::int64_t>::Fail(ErrorCode::kDatabaseError, mysql_error(conn->handle()));
    }
    MYSQL_RES* result = mysql_store_result(conn->handle());
    if (result == nullptr) {
        return Result<std::int64_t>::Fail(ErrorCode::kDatabaseError, mysql_error(conn->handle()));
    }

    std::int64_t found_id = 0;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row != nullptr && row[0] != nullptr) {
        try {
            found_id = std::stoll(row[0]);
        } catch (...) {
            found_id = 0;
        }
    }
    mysql_free_result(result);

    // 0 表示没找到，调用方据此决定是插入还是更新
    return Result<std::int64_t>::Ok(found_id);
}

Result<std::int64_t> MySqlCourseRepository::Insert(const Course& course) {
    auto conn = pool_.Acquire();
    if (!conn) {
        return Result<std::int64_t>::Fail(ErrorCode::kDatabaseError, "数据库连接不可用");
    }

    if (mysql_query(conn->handle(), "START TRANSACTION") != 0) {
        return Result<std::int64_t>::Fail(ErrorCode::kDatabaseError, mysql_error(conn->handle()));
    }

    const std::string sql =
        "INSERT INTO courses (name, code, teacher, credits, semester, source) VALUES (" +
        conn->Quote(course.name) + "," + conn->Quote(course.code) + "," +
        conn->Quote(course.teacher) + "," + std::to_string(course.credits) + "," +
        conn->Quote(course.semester) + "," + conn->Quote(course.source) + ")";

    if (mysql_query(conn->handle(), sql.c_str()) != 0) {
        // mysql_errno 必须在任何后续查询之前取，ROLLBACK 会把它清掉
        const unsigned int err = mysql_errno(conn->handle());
        const std::string message = mysql_error(conn->handle());
        mysql_query(conn->handle(), "ROLLBACK");
        if (err == kDuplicateEntryError) {
            return Result<std::int64_t>::Fail(
                ErrorCode::kConflict,
                "课程号 " + course.code + " 在该学期已存在（唯一键冲突）");
        }
        return Result<std::int64_t>::Fail(ErrorCode::kDatabaseError, message);
    }

    const auto new_id = static_cast<std::int64_t>(mysql_insert_id(conn->handle()));

    std::string error;
    if (!InsertSessions(*conn, new_id, course, error)) {
        mysql_query(conn->handle(), "ROLLBACK");
        return Result<std::int64_t>::Fail(ErrorCode::kDatabaseError, error);
    }

    if (mysql_query(conn->handle(), "COMMIT") != 0) {
        return Result<std::int64_t>::Fail(ErrorCode::kDatabaseError, mysql_error(conn->handle()));
    }

    return Result<std::int64_t>::Ok(new_id);
}

Result<bool> MySqlCourseRepository::Update(const Course& course) {
    auto conn = pool_.Acquire();
    if (!conn) {
        return Result<bool>::Fail(ErrorCode::kDatabaseError, "数据库连接不可用");
    }

    if (mysql_query(conn->handle(), "START TRANSACTION") != 0) {
        return Result<bool>::Fail(ErrorCode::kDatabaseError, mysql_error(conn->handle()));
    }

    const std::string sql =
        "UPDATE courses SET name = " + conn->Quote(course.name) + ", code = " +
        conn->Quote(course.code) + ", teacher = " + conn->Quote(course.teacher) +
        ", credits = " + std::to_string(course.credits) + ", semester = " +
        conn->Quote(course.semester) + ", source = " + conn->Quote(course.source) +
        " WHERE id = " + std::to_string(course.id);

    if (mysql_query(conn->handle(), sql.c_str()) != 0) {
        const unsigned int err = mysql_errno(conn->handle());
        const std::string message = mysql_error(conn->handle());
        mysql_query(conn->handle(), "ROLLBACK");
        if (err == kDuplicateEntryError) {
            return Result<bool>::Fail(
                ErrorCode::kConflict,
                "课程号 " + course.code + " 在该学期已被其它课程占用（唯一键冲突）");
        }
        return Result<bool>::Fail(ErrorCode::kDatabaseError, message);
    }

    // 判断课程是否真的存在：更新 0 行可能是「不存在」也可能是「值没变」
    const std::string exists_sql = "SELECT id FROM courses WHERE id = " + std::to_string(course.id);
    if (mysql_query(conn->handle(), exists_sql.c_str()) != 0) {
        mysql_query(conn->handle(), "ROLLBACK");
        return Result<bool>::Fail(ErrorCode::kDatabaseError, mysql_error(conn->handle()));
    }
    MYSQL_RES* result = mysql_store_result(conn->handle());
    const bool exists = result != nullptr && mysql_num_rows(result) > 0;
    if (result != nullptr) {
        mysql_free_result(result);
    }
    if (!exists) {
        mysql_query(conn->handle(), "ROLLBACK");
        return Result<bool>::Fail(ErrorCode::kNotFound,
                                  "课程不存在: id=" + std::to_string(course.id));
    }

    // 课次整体替换：逻辑最简单，也不容易出错
    const std::string delete_sql =
        "DELETE FROM course_sessions WHERE course_id = " + std::to_string(course.id);
    if (mysql_query(conn->handle(), delete_sql.c_str()) != 0) {
        const std::string message = mysql_error(conn->handle());
        mysql_query(conn->handle(), "ROLLBACK");
        return Result<bool>::Fail(ErrorCode::kDatabaseError, message);
    }

    std::string error;
    if (!InsertSessions(*conn, course.id, course, error)) {
        mysql_query(conn->handle(), "ROLLBACK");
        return Result<bool>::Fail(ErrorCode::kDatabaseError, error);
    }

    if (mysql_query(conn->handle(), "COMMIT") != 0) {
        return Result<bool>::Fail(ErrorCode::kDatabaseError, mysql_error(conn->handle()));
    }
    return Result<bool>::Ok(true);
}

Result<bool> MySqlCourseRepository::Delete(std::int64_t id) {
    auto conn = pool_.Acquire();
    if (!conn) {
        return Result<bool>::Fail(ErrorCode::kDatabaseError, "数据库连接不可用");
    }

    // course_sessions 有 ON DELETE CASCADE，会自动跟着删掉
    const std::string sql = "DELETE FROM courses WHERE id = " + std::to_string(id);
    if (mysql_query(conn->handle(), sql.c_str()) != 0) {
        return Result<bool>::Fail(ErrorCode::kDatabaseError, mysql_error(conn->handle()));
    }
    if (mysql_affected_rows(conn->handle()) == 0) {
        return Result<bool>::Fail(ErrorCode::kNotFound, "课程不存在: id=" + std::to_string(id));
    }
    return Result<bool>::Ok(true);
}

}  // namespace campus

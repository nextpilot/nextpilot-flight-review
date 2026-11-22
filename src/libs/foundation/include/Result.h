#pragma once

#include <QString>

namespace nextpilot::plotscope::foundation
{

// 统一的轻量错误结果。避免到处抛异常或用 bool + 输出参数。
struct Error
{
    QString code;
    QString message;
    bool empty() const { return code.isEmpty(); }
};

template <typename T>
class Result
{
public:
    Result(const T &value) : m_value(value), m_ok(true) {}
    Result(T &&value) : m_value(std::move(value)), m_ok(true) {}
    Result(const Error &error) : m_error(error), m_ok(false) {}

    bool ok() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const T &value() const { return m_value; }
    T &value() { return m_value; }
    const Error &error() const { return m_error; }

private:
    T m_value{};
    Error m_error;
    bool m_ok = false;
};

// void 特化：只需要成功/失败与错误信息。
template <>
class Result<void>
{
public:
    Result() : m_ok(true) {}
    Result(const Error &error) : m_error(error), m_ok(false) {}

    bool ok() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const Error &error() const { return m_error; }

private:
    Error m_error;
    bool m_ok = true;
};

inline Error fail(const QString &code, const QString &message)
{
    return Error{code, message};
}

} // namespace nextpilot::plotscope::foundation


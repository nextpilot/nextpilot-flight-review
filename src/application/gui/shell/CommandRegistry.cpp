#include "CommandRegistry.h"

#include <QStringList>

namespace nextpilot::plotscope::gui
{

namespace
{
enum class TokenKind
{
    End,
    Identifier,
    String,
    True,
    False,
    Equal,
    NotEqual,
    And,
    Or,
    LeftParen,
    RightParen,
    Invalid
};

struct Token
{
    TokenKind kind = TokenKind::End;
    QString text;
};

class Lexer
{
public:
    explicit Lexer(const QString &source) : m_source(source) {}

    Token next()
    {
        while (m_pos < m_source.size() && m_source.at(m_pos).isSpace())
            ++m_pos;
        if (m_pos >= m_source.size())
            return Token{TokenKind::End, QString()};

        const QChar ch = m_source.at(m_pos);
        if (ch == QLatin1Char('&') && m_pos + 1 < m_source.size()
            && m_source.at(m_pos + 1) == QLatin1Char('&'))
        {
            m_pos += 2;
            return Token{TokenKind::And, QStringLiteral("&&")};
        }
        if (ch == QLatin1Char('|') && m_pos + 1 < m_source.size()
            && m_source.at(m_pos + 1) == QLatin1Char('|'))
        {
            m_pos += 2;
            return Token{TokenKind::Or, QStringLiteral("||")};
        }
        if (ch == QLatin1Char('=') && m_pos + 1 < m_source.size()
            && m_source.at(m_pos + 1) == QLatin1Char('='))
        {
            m_pos += 2;
            return Token{TokenKind::Equal, QStringLiteral("==")};
        }
        if (ch == QLatin1Char('!') && m_pos + 1 < m_source.size()
            && m_source.at(m_pos + 1) == QLatin1Char('='))
        {
            m_pos += 2;
            return Token{TokenKind::NotEqual, QStringLiteral("!=")};
        }
        if (ch == QLatin1Char('('))
        {
            ++m_pos;
            return Token{TokenKind::LeftParen, QStringLiteral("(")};
        }
        if (ch == QLatin1Char(')'))
        {
            ++m_pos;
            return Token{TokenKind::RightParen, QStringLiteral(")")};
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
        {
            const QChar quote = ch;
            ++m_pos;
            QString text;
            while (m_pos < m_source.size() && m_source.at(m_pos) != quote)
            {
                text += m_source.at(m_pos);
                ++m_pos;
            }
            if (m_pos >= m_source.size())
                return Token{TokenKind::Invalid, QString()};
            ++m_pos;
            return Token{TokenKind::String, text};
        }
        if (ch.isLetter() || ch.isDigit() || ch == QLatin1Char('_') || ch == QLatin1Char('.'))
        {
            QString text;
            while (m_pos < m_source.size())
            {
                const QChar c = m_source.at(m_pos);
                if ((!c.isLetter() && !c.isDigit()) && c != QLatin1Char('_') && c != QLatin1Char('.'))
                    break;
                text += c;
                ++m_pos;
            }
            if (text == QStringLiteral("true"))
                return Token{TokenKind::True, text};
            if (text == QStringLiteral("false"))
                return Token{TokenKind::False, text};
            return Token{TokenKind::Identifier, text};
        }

        ++m_pos;
        return Token{TokenKind::Invalid, QString(ch)};
    }

private:
    QString m_source;
    int m_pos = 0;
};

class WhenParser
{
public:
    WhenParser(const QString &expression, const QHash<QString, QVariant> &context)
        : m_lexer(expression), m_context(context)
    {
        m_current = m_lexer.next();
    }

    bool parse()
    {
        if (m_current.kind == TokenKind::End)
            return true;
        const bool value = parseOr();
        if (!m_ok || m_current.kind != TokenKind::End)
        {
            m_ok = false;
            return false;
        }
        return value;
    }

    QString error() const { return m_error; }

private:
    bool parseOr()
    {
        bool value = parseAnd();
        while (m_ok && m_current.kind == TokenKind::Or)
        {
            advance();
            const bool rhs = parseAnd();
            value = value || rhs;
        }
        return value;
    }

    bool parseAnd()
    {
        bool value = parseCompare();
        while (m_ok && m_current.kind == TokenKind::And)
        {
            advance();
            const bool rhs = parseCompare();
            value = value && rhs;
        }
        return value;
    }

    bool parseCompare()
    {
        const QString lhs = parseValue();
        if (!m_ok)
            return false;
        if (m_current.kind == TokenKind::Equal || m_current.kind == TokenKind::NotEqual)
        {
            const bool equal = m_current.kind == TokenKind::Equal;
            advance();
            const QString rhs = parseValue();
            if (!m_ok)
                return false;
            return equal ? lhs == rhs : lhs != rhs;
        }
        return lhs == QStringLiteral("true") || (!lhs.isEmpty() && lhs != QStringLiteral("false"));
    }

    QString parseValue()
    {
        if (!m_ok)
            return QString();

        if (m_current.kind == TokenKind::String)
        {
            const QString text = m_current.text;
            advance();
            return text;
        }
        if (m_current.kind == TokenKind::True || m_current.kind == TokenKind::False)
        {
            const QString text = m_current.kind == TokenKind::True ? QStringLiteral("true")
                                                                   : QStringLiteral("false");
            advance();
            return text;
        }
        if (m_current.kind == TokenKind::Identifier)
        {
            const QString key = m_current.text;
            advance();
            return m_context.value(key).toString();
        }
        if (m_current.kind == TokenKind::LeftParen)
        {
            advance();
            const bool nested = parseOr();
            if (m_current.kind != TokenKind::RightParen)
            {
                m_ok = false;
                m_error = QStringLiteral("缺少右括号");
                return QString();
            }
            advance();
            return nested ? QStringLiteral("true") : QStringLiteral("false");
        }

        m_ok = false;
        m_error = QStringLiteral("when 表达式无效");
        return QString();
    }

    void advance() { m_current = m_lexer.next(); }

    Lexer m_lexer;
    const QHash<QString, QVariant> &m_context;
    Token m_current;
    bool m_ok = true;
    QString m_error;
};
} // namespace

bool CommandRegistry::add(const CommandDescriptor &command)
{
    if (command.id.isEmpty() || command.title.isEmpty() || !command.handler)
        return false;
    for (const CommandDescriptor &existing : m_commands)
        if (existing.id == command.id)
            return false;
    m_commands.append(command);
    return true;
}

bool CommandRegistry::contains(const QString &id) const
{
    return find(id) != nullptr;
}

const CommandDescriptor *CommandRegistry::find(const QString &id) const
{
    for (const CommandDescriptor &command : m_commands)
        if (command.id == id)
            return &command;
    return nullptr;
}

void CommandRegistry::setContextValue(const QString &key, const QVariant &value)
{
    if (key.isEmpty())
        return;
    if (!value.isValid() || value.isNull())
        m_context.remove(key);
    else
        m_context.insert(key, value);
}

QVariant CommandRegistry::contextValue(const QString &key) const
{
    return m_context.value(key);
}

void CommandRegistry::clearContext()
{
    m_context.clear();
}

bool CommandRegistry::execute(const QString &id, QString *error)
{
    const CommandDescriptor *command = find(id);
    if (!command)
    {
        if (error)
            *error = QStringLiteral("命令不存在: %1").arg(id);
        return false;
    }

    if (!command->when.isEmpty())
    {
        bool ok = false;
        if (!evaluateWhen(command->when, m_context, &ok) || !ok)
        {
            if (error)
                *error = QStringLiteral("命令当前不可用: %1").arg(command->when);
            return false;
        }
    }
    if (command->disabledReason)
    {
        const QString reason = command->disabledReason();
        if (!reason.isEmpty())
        {
            if (error)
                *error = reason;
            return false;
        }
    }

    command->handler();
    return true;
}

bool CommandRegistry::evaluateWhen(const QString &expression, const QHash<QString, QVariant> &context,
                                   bool *ok)
{
    WhenParser parser(expression, context);
    const bool result = parser.parse();
    if (ok)
        *ok = result || parser.error().isEmpty();
    return result;
}

} // namespace nextpilot::plotscope::gui

#include "command/WhenExpression.h"
#include "command/ContextStack.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVariantList>

#include <cmath>
#include <vector>

namespace nextpilot::plotscope::command
{

enum class Op
{
    And,
    Or,
    Not,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    In,
    NotIn,
    Regex,
    NotRegex,
    Literal,
    Key,
};

struct WhenNode;
using WhenNodePtr = std::shared_ptr<const WhenNode>;

// when 表达式树节点（实现细节，public 头里只有前置声明）。
struct WhenNode
{
    Op op = Op::Literal;
    QString text;                 // Key 的键名 / 字符串字面量 / 正则源
    QVariant literal;             // 数字/布尔/字符串字面量
    std::shared_ptr<QRegularExpression> regex;
    std::vector<WhenNodePtr> kids;
    std::vector<QVariant> list;   // in 右侧集合
};

namespace
{

using Node = WhenNode;
using NodePtr = WhenNodePtr;

// ---- 词法 ----

enum class TokenKind
{
    End,
    Identifier,
    String,
    Number,
    True,
    False,
    And,
    Or,
    Not,
    In,
    LParen,
    RParen,
    Comma,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    Regex,
};

struct Token
{
    TokenKind kind = TokenKind::End;
    QString text;
};

class Lexer
{
public:
    explicit Lexer(QString source) : m_source(std::move(source)) {}

    Token next()
    {
        skipSpace();
        if (m_pos >= m_source.size())
            return {};
        const QChar c = m_source.at(m_pos);

        if (c == '(') { ++m_pos; return {TokenKind::LParen}; }
        if (c == ')') { ++m_pos; return {TokenKind::RParen}; }
        if (c == ',') { ++m_pos; return {TokenKind::Comma}; }

        if (c == '&' && peek(1) == '&') { m_pos += 2; return {TokenKind::And}; }
        if (c == '|' && peek(1) == '|') { m_pos += 2; return {TokenKind::Or}; }
        if (c == '!' && peek(1) == '=') { m_pos += 2; return {TokenKind::Ne}; }
        if (c == '!') { ++m_pos; return {TokenKind::Not}; }
        if (c == '=' && peek(1) == '=') { m_pos += 2; return {TokenKind::Eq}; }
        if (c == '=' && peek(1) == '~') { m_pos += 2; return {TokenKind::Regex}; }
        if (c == '<' && peek(1) == '=') { m_pos += 2; return {TokenKind::Le}; }
        if (c == '>' && peek(1) == '=') { m_pos += 2; return {TokenKind::Ge}; }
        if (c == '<') { ++m_pos; return {TokenKind::Lt}; }
        if (c == '>') { ++m_pos; return {TokenKind::Gt}; }

        if (c == '"' || c == '\'')
            return string(c);

        if (c.isLetter() || c == '_')
        {
            const int start = m_pos;
            while (m_pos < m_source.size())
            {
                const QChar k = m_source.at(m_pos);
                if (!(k.isLetterOrNumber() || k == '_' || k == '.' || k == ':' || k == '-'))
                    break;
                ++m_pos;
            }
            const QString word = m_source.mid(start, m_pos - start);
            if (word == QLatin1String("true"))
                return {TokenKind::True};
            if (word == QLatin1String("false"))
                return {TokenKind::False};
            if (word == QLatin1String("in"))
                return {TokenKind::In};
            if (word == QLatin1String("not"))
                return {TokenKind::Not};
            return {TokenKind::Identifier, word};
        }

        if (c.isDigit() || c == '-' || c == '+')
        {
            const int start = m_pos;
            if (c == '-' || c == '+')
                ++m_pos;
            bool seenDot = false;
            while (m_pos < m_source.size())
            {
                const QChar k = m_source.at(m_pos);
                if (k.isDigit()) { ++m_pos; continue; }
                if (k == '.' && !seenDot) { seenDot = true; ++m_pos; continue; }
                break;
            }
            const QString num = m_source.mid(start, m_pos - start);
            bool okNum = false;
            num.toDouble(&okNum);
            if (okNum)
                return {TokenKind::Number, num};
            m_pos = start;
        }

        // 未知字符：吞掉，交给上层判为解析失败。
        ++m_pos;
        return {TokenKind::Identifier, QString()};
    }

private:
    QChar peek(int offset) const
    {
        const int at = m_pos + offset;
        return at < m_source.size() ? m_source.at(at) : QChar();
    }

    void skipSpace()
    {
        while (m_pos < m_source.size() && m_source.at(m_pos).isSpace())
            ++m_pos;
    }

    Token string(QChar quote)
    {
        ++m_pos;
        const int start = m_pos;
        QString out;
        while (m_pos < m_source.size())
        {
            const QChar c = m_source.at(m_pos);
            if (c == quote)
                break;
            if (c == '\\' && m_pos + 1 < m_source.size())
            {
                ++m_pos;
                out.append(m_source.at(m_pos));
                ++m_pos;
                continue;
            }
            out.append(c);
            ++m_pos;
        }
        if (m_pos >= m_source.size())
        {
            // 引号没闭合
            m_error = QStringLiteral("字符串缺少结束引号");
            return {TokenKind::Identifier, QString()};
        }
        ++m_pos;
        Q_UNUSED(start);
        return {TokenKind::String, out};
    }

public:
    QString error() const { return m_error; }

private:
    QString m_source;
    int m_pos = 0;
    QString m_error;
};

class Parser
{
public:
    explicit Parser(QString source) : m_lexer(std::move(source)) { advance(); }

    NodePtr parse()
    {
        NodePtr node = parseOr();
        if (!m_ok)
            return nullptr;
        if (m_current.kind != TokenKind::End)
        {
            fail(QStringLiteral("表达式末尾有多余内容"));
            return nullptr;
        }
        return node;
    }

    QString error() const { return m_error; }

private:
    NodePtr parseOr()
    {
        NodePtr left = parseAnd();
        while (m_ok && m_current.kind == TokenKind::Or)
        {
            advance();
            NodePtr right = parseAnd();
            if (!m_ok)
                return nullptr;
            auto node = std::make_shared<Node>();
            node->op = Op::Or;
            node->kids = {left, right};
            left = node;
        }
        return left;
    }

    NodePtr parseAnd()
    {
        NodePtr left = parseUnary();
        while (m_ok && m_current.kind == TokenKind::And)
        {
            advance();
            NodePtr right = parseUnary();
            if (!m_ok)
                return nullptr;
            auto node = std::make_shared<Node>();
            node->op = Op::And;
            node->kids = {left, right};
            left = node;
        }
        return left;
    }

    NodePtr parseUnary()
    {
        if (m_current.kind == TokenKind::Not)
        {
            advance();
            // 前缀 not 只作一元否定；"not in" / "not =~" 的后缀形式在 parseComparison 里处理。
            NodePtr inner = parseUnary();
            if (!m_ok)
                return nullptr;
            auto node = std::make_shared<Node>();
            node->op = Op::Not;
            node->kids = {inner};
            return node;
        }
        return parseComparison(false);
    }

    NodePtr parseComparison(bool /*negate*/ = false)
    {
        NodePtr left = parsePrimaryValue();
        if (!m_ok)
            return nullptr;

        Op op = Op::Literal;
        bool matched = true;
        switch (m_current.kind)
        {
        case TokenKind::Eq: op = Op::Eq; break;
        case TokenKind::Ne: op = Op::Ne; break;
        case TokenKind::Lt: op = Op::Lt; break;
        case TokenKind::Le: op = Op::Le; break;
        case TokenKind::Gt: op = Op::Gt; break;
        case TokenKind::Ge: op = Op::Ge; break;
        case TokenKind::In: op = Op::In; break;
        case TokenKind::Regex: op = Op::Regex; break;
        default: matched = false; break;
        }

        // 后缀形式 "x not in (...)" / "x not =~ "...""：not 出现在左值之后。
        if (!matched && m_current.kind == TokenKind::Not)
        {
            advance();
            if (m_current.kind == TokenKind::In)
            {
                op = Op::NotIn;
                matched = true;
            }
            else if (m_current.kind == TokenKind::Regex)
            {
                op = Op::NotRegex;
                matched = true;
            }
            else
            {
                fail(QStringLiteral("not 之后只能跟 in 或 =~"));
                return nullptr;
            }
        }

        if (!matched)
            return left;
        advance();

        if (op == Op::In || op == Op::NotIn)
            return makeIn(left, op);

        NodePtr right = parsePrimaryValue();
        if (!m_ok)
            return nullptr;

        if (op == Op::Regex || op == Op::NotRegex)
        {
            // 右侧必须是字符串字面量或键；编译成正则节点。
            QString pattern;
            if (right->op == Op::Literal)
                pattern = right->literal.toString();
            if (pattern.isEmpty())
            {
                fail(QStringLiteral("=~ 右侧必须是字符串"));
                return nullptr;
            }
            auto node = std::make_shared<Node>();
            node->op = op;
            node->kids = {left};
            node->regex = std::make_shared<QRegularExpression>(pattern);
            if (!node->regex->isValid())
                fail(QStringLiteral("正则无效: ") + pattern);
            return node;
        }

        auto node = std::make_shared<Node>();
        node->op = op;
        node->kids = {left, right};
        return node;
    }

    NodePtr makeIn(NodePtr left, Op op)
    {
        auto node = std::make_shared<Node>();
        node->op = op;
        node->kids = {left};

        if (m_current.kind == TokenKind::LParen)
        {
            advance();
            if (m_current.kind == TokenKind::RParen)
            {
                advance();
                return node;   // 空集合：in 恒假
            }
            while (m_ok)
            {
                NodePtr item = parsePrimaryValue();
                if (!m_ok)
                    return nullptr;
                if (item->op != Op::Literal)
                {
                    fail(QStringLiteral("in 右侧只接受字面量"));
                    return nullptr;
                }
                node->list.push_back(item->literal);
                if (m_current.kind == TokenKind::Comma)
                {
                    advance();
                    continue;
                }
                break;
            }
            if (m_current.kind != TokenKind::RParen)
            {
                fail(QStringLiteral("in 集合缺少右括号"));
                return nullptr;
            }
            advance();
            return node;
        }

        NodePtr single = parsePrimaryValue();
        if (!m_ok)
            return nullptr;
        if (single->op != Op::Literal)
        {
            fail(QStringLiteral("in 右侧只接受字面量或集合"));
            return nullptr;
        }
        node->list.push_back(single->literal);
        return node;
    }

    NodePtr parsePrimaryValue()
    {
        switch (m_current.kind)
        {
        case TokenKind::LParen:
        {
            advance();
            NodePtr inner = parseOr();
            if (!m_ok)
                return nullptr;
            if (m_current.kind != TokenKind::RParen)
            {
                fail(QStringLiteral("缺少右括号"));
                return nullptr;
            }
            advance();
            return inner;
        }
        case TokenKind::String:
        case TokenKind::Number:
        case TokenKind::True:
        case TokenKind::False:
        {
            auto node = std::make_shared<Node>();
            node->op = Op::Literal;
            if (m_current.kind == TokenKind::String)
                node->literal = m_current.text;
            else if (m_current.kind == TokenKind::Number)
                node->literal = m_current.text.toDouble();
            else
                node->literal = (m_current.kind == TokenKind::True);
            node->text = m_current.text;
            advance();
            return node;
        }
        case TokenKind::Identifier:
        {
            auto node = std::make_shared<Node>();
            node->op = Op::Key;
            node->text = m_current.text;
            advance();
            return node;
        }
        default:
            fail(QStringLiteral("需要一个值，实际为: ") + tokenName(m_current.kind));
            return nullptr;
        }
    }

    static QString tokenName(TokenKind kind)
    {
        switch (kind)
        {
        case TokenKind::End: return QStringLiteral("<结尾>");
        case TokenKind::LParen: return QStringLiteral("(");
        case TokenKind::RParen: return QStringLiteral(")");
        case TokenKind::Comma: return QStringLiteral(",");
        case TokenKind::And: return QStringLiteral("&&");
        case TokenKind::Or: return QStringLiteral("||");
        default: return QStringLiteral("<token>");
        }
    }

    void advance()
    {
        m_current = m_lexer.next();
        if (!m_lexer.error().isEmpty() && m_ok)
            fail(m_lexer.error());
    }

    void fail(const QString &message)
    {
        if (m_ok)
        {
            m_ok = false;
            m_error = message;
        }
    }

    Lexer m_lexer;
    Token m_current;
    bool m_ok = true;
    QString m_error;
};

// ---- 求值 ----

bool asNumber(const QVariant &value, double *out)
{
    if (!value.isValid())
        return false;
    switch (value.typeId())
    {
    case QMetaType::Bool:
        return false;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        *out = value.toDouble();
        return true;
    default:
        break;
    }
    if (value.typeId() == QMetaType::QString)
    {
        bool okNum = false;
        const double parsed = value.toString().toDouble(&okNum);
        if (okNum)
        {
            *out = parsed;
            return true;
        }
    }
    return false;
}

int compareVariants(const QVariant &lhs, const QVariant &rhs)
{
    double a = 0.0;
    double b = 0.0;
    if (asNumber(lhs, &a) && asNumber(rhs, &b))
        return a < b ? -1 : (a > b ? 1 : 0);
    // 字符串比较忽略大小写（与 VS Code 的键值比较口径一致）。
    return QString::compare(lhs.toString(), rhs.toString(), Qt::CaseInsensitive);
}

bool sameValue(const QVariant &lhs, const QVariant &rhs)
{
    if (!lhs.isValid() || !rhs.isValid())
        return false;
    if (lhs.typeId() == QMetaType::Bool || rhs.typeId() == QMetaType::Bool)
        return lhs.toBool() == rhs.toBool();
    return compareVariants(lhs, rhs) == 0;
}

QList<QVariant> expandList(const QVariant &value)
{
    QList<QVariant> out;
    if (value.typeId() == QMetaType::QVariantList)
    {
        for (const QVariant &v : value.toList())
            out.push_back(v);
        return out;
    }
    if (value.typeId() == QMetaType::QStringList)
    {
        for (const QString &s : value.toStringList())
            out.push_back(s);
        return out;
    }
    out.push_back(value);
    return out;
}

bool evalNode(const Node &node, const ContextStack &context);

QVariant evalValue(const Node &node, const ContextStack &context)
{
    if (node.op == Op::Literal)
        return node.literal;
    if (node.op == Op::Key)
    {
        // 键本身可以带括号分组（如 (a || b) 已被 parsePrimaryValue 折成布尔节点），此处只取键值。
        return context.value(node.text);
    }
    if (node.op == Op::Not)
        return !evalNode(*node.kids.front(), context);
    return evalNode(node, context);   // 布尔子表达式当作 QVariant(bool)
}

bool evalNode(const Node &node, const ContextStack &context)
{
    switch (node.op)
    {
    case Op::Literal:
        if (node.literal.typeId() == QMetaType::Bool)
            return node.literal.toBool();
        if (node.literal.typeId() == QMetaType::Double)
            return std::abs(node.literal.toDouble()) > 0.0;
        return !node.literal.toString().isEmpty();

    case Op::Key:
    {
        const QVariant v = context.value(node.text);
        if (!v.isValid())
            return false;
        if (v.typeId() == QMetaType::Bool)
            return v.toBool();
        double num = 0.0;
        if (asNumber(v, &num))
            return std::abs(num) > 0.0;
        return !v.toString().isEmpty();
    }

    case Op::Not:
        return !evalNode(*node.kids.front(), context);

    case Op::And:
        return evalNode(*node.kids[0], context) && evalNode(*node.kids[1], context);

    case Op::Or:
        return evalNode(*node.kids[0], context) || evalNode(*node.kids[1], context);

    case Op::Eq:
        return sameValue(evalValue(*node.kids[0], context), evalValue(*node.kids[1], context));

    case Op::Ne:
        return !sameValue(evalValue(*node.kids[0], context), evalValue(*node.kids[1], context));

    case Op::Lt:
        return compareVariants(evalValue(*node.kids[0], context), evalValue(*node.kids[1], context)) < 0;
    case Op::Le:
        return compareVariants(evalValue(*node.kids[0], context), evalValue(*node.kids[1], context)) <= 0;
    case Op::Gt:
        return compareVariants(evalValue(*node.kids[0], context), evalValue(*node.kids[1], context)) > 0;
    case Op::Ge:
        return compareVariants(evalValue(*node.kids[0], context), evalValue(*node.kids[1], context)) >= 0;

    case Op::In:
    case Op::NotIn:
    {
        bool hit = false;
        for (const QVariant &candidate : expandList(evalValue(*node.kids.front(), context)))
        {
            for (const QVariant &member : node.list)
            {
                if (sameValue(candidate, member))
                {
                    hit = true;
                    break;
                }
            }
            if (hit)
                break;
        }
        return node.op == Op::In ? hit : !hit;
    }

    case Op::Regex:
    case Op::NotRegex:
    {
        const bool matched = node.regex && node.regex->isValid() &&
                             node.regex->match(evalValue(*node.kids.front(), context).toString()).hasMatch();
        return node.op == Op::Regex ? matched : !matched;
    }
    }
    return false;
}

} // namespace

WhenExpression WhenExpression::compile(const QString &source, QString *error)
{
    WhenExpression expression;
    expression.m_source = source;
    if (source.trimmed().isEmpty())
    {
        expression.empty_ = true;
        expression.valid_ = true;
        return expression;
    }

    expression.empty_ = false;
    Parser parser(source);
    NodePtr root = parser.parse();
    if (!root || !parser.error().isEmpty())
    {
        expression.valid_ = false;
        // 解析失败折成恒假：不抛异常，调用方把 error 送进诊断。
        auto node = std::make_shared<Node>();
        node->op = Op::Literal;
        node->literal = false;
        expression.m_root = node;
        if (error)
            *error = parser.error().isEmpty() ? QStringLiteral("when 解析失败") : parser.error();
        return expression;
    }
    expression.valid_ = true;
    expression.m_root = root;
    return expression;
}

bool WhenExpression::evaluate(const ContextStack &context) const
{
    if (empty_)
        return true;
    if (!m_root)
        return false;
    return evalNode(*m_root, context);
}

} // namespace nextpilot::plotscope::command

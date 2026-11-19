#pragma once

#include "IToolContribution.h"
#include "Domain.h"

#include <QList>
#include <QString>

#include <memory>
#include <vector>

namespace nextpilot::plotscope::registry
{

// 知识包：一个业务方向（或其下的一个功能单元）对外提供的全部工具贡献的聚合。
//
// 它是**装载/卸载的原子单位**："装上飞控包" = 一次性登记它的格式 + UI + 脚本 +
// 数据 + 例子 + 文档；"卸掉飞控包" = 一次性撤销全部，不留半套能力。
//
// 一个 contribution 的 provider() 必须等于它所属 pack 的 id——注册表据此把贡献
// 归到包，实现按包原子装卸；这也是"扩展之间互不牵连"的保证。
class IToolPack
{
public:
    virtual ~IToolPack() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual Domain domain() const = 0;

    // 该包提供的全部贡献（所有权归包本身，由 unique_ptr 持有）。
    virtual const std::vector<std::unique_ptr<IToolContribution>> &contributions() const = 0;

    // 可变访问：仅供注册阶段把贡献"移交"给注册表接管（转移 unique_ptr 所有权）。
    virtual std::vector<std::unique_ptr<IToolContribution>> &contributions() = 0;
};

// 通用知识包实现：持有一组贡献，是绝大多数内置/外部包的现成基类。
class ToolPack final : public IToolPack
{
public:
    ToolPack(QString id, QString displayName, Domain domain)
        : m_id(std::move(id)), m_displayName(std::move(displayName)), m_domain(domain) {}

    QString id() const override { return m_id; }
    QString displayName() const override { return m_displayName; }
    Domain domain() const override { return m_domain; }

    const std::vector<std::unique_ptr<IToolContribution>> &contributions() const override
    {
        return m_contribs;
    }
    std::vector<std::unique_ptr<IToolContribution>> &contributions() override
    {
        return m_contribs;
    }

    // 便捷追加：构造期组装一个包。
    template <typename T, typename... Args>
    T *add(Args &&...args)
    {
        auto obj = std::make_unique<T>(std::forward<Args>(args)...);
        T *raw = obj.get();
        raw->setProvider(m_id); // 贡献自动记住所属包，供注册表按包原子装卸
        m_contribs.push_back(std::move(obj));
        return raw;
    }

private:
    QString m_id;
    QString m_displayName;
    Domain m_domain;
    std::vector<std::unique_ptr<IToolContribution>> m_contribs;
};

} // namespace nextpilot::plotscope::registry

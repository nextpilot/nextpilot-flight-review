#pragma once
/* 过渡适配：把旧 PsLoaderVtable 包成统一 DataInput vtable。
 * 文件解析仍在原 loader 内，宿主只看到 PS_INPUT_FILE 能力。 */

#include "ps_plugin_api.h"
#include "PsDataInputBase.hpp"

#include <cstdio>
#include <cstring>
#include <string>

template <const PsLoaderVtable *V>
class LegacyFileInput final : public ps::DataInputBase
{
public:
    LegacyFileInput()
    {
        char buf[64 * 1024] = {0};
        if (V->schema_json && V->schema_json(buf, sizeof(buf)) >= 0)
            schema_.assign(buf);
        std::memset(buf, 0, sizeof(buf));
        if (V->default_options && V->default_options("", buf, sizeof(buf)) >= 0)
            defaults_.assign(buf);
        if (defaults_.empty())
            defaults_ = "{}";
    }

    uint64_t capabilities() const override
    {
        return PS_INPUT_FILE | PS_INPUT_DIALOG;
    }

    bool importFile(std::string_view path, std::string_view opts, void **dataset, PsError *err) override
    {
        if (!dataset || path.empty())
        {
            ps::fillError(err, 2, "缺少文件路径或 dataset 输出");
            return false;
        }
        char e[1024] = {0};
        const std::string p(path);
        const std::string o(opts.empty() ? "{}" : opts);
        const int32_t rc = V->load(p.c_str(), o.c_str(), host(), dataset, e, sizeof(e));
        if (rc != 0)
        {
            ps::fillError(err, rc, e[0] ? e : "loader 失败");
            return false;
        }
        return true;
    }

    PsInputDialog dialog() const override
    {
        return PsInputDialog{nullptr, schema_.c_str(), defaults_.c_str()};
    }

    PsInputState currentState() const override { return PS_INPUT_IDLE; }

    bool start(PsError *err) override
    {
        (void)err;
        return true;
    }

    void stop() override {}

private:
    std::string schema_;
    std::string defaults_;
};

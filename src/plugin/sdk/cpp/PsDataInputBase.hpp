#pragma once
/* PlotScope C++ 插件 SDK：只做 C++ class -> C vtable 的边界转换。
 * 不改 ps_plugin_api.h 的布局，不引入 Qt / kernel 依赖。
 *
 * 用法：
 *   class MySource final : public ps::DataInputBase { ... };
 *   PS_DATA_INPUT_PLUGIN(MySource, R"({"id":"my_source",...})")
 */

#include "ps_plugin_api.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>

namespace ps {

inline void fillError(PsError *err, int32_t code, std::string_view message) noexcept
{
    if (!err)
        return;
    err->code = code;
    const size_t n = std::min(message.size(), sizeof(err->message) - 1);
    if (n)
        std::memcpy(err->message, message.data(), n);
    err->message[n] = '\0';
}

inline PsStringView viewOf(const std::string &s) noexcept
{
    return PsStringView{s.data(), static_cast<uint32_t>(s.size())};
}

inline std::string_view stringViewOf(PsStringView v) noexcept
{
    return v.data ? std::string_view(v.data, v.size) : std::string_view{};
}

class DataInputBase
{
public:
    virtual ~DataInputBase() = default;

    /* 必须实现：能力位决定宿主把它放进文件列表还是流列表。 */
    virtual uint64_t capabilities() const = 0;

    /* 生命周期。start 前宿主保证 bind + load_config 已调用。 */
    virtual bool start(PsError *err) = 0;
    virtual void stop() = 0;
    virtual bool poll(PsError *err) { (void)err; return true; }

    virtual bool pause(PsError *err)
    {
        fillError(err, 1, "pause is not supported");
        return false;
    }
    virtual bool resume(PsError *err)
    {
        fillError(err, 1, "resume is not supported");
        return false;
    }

    virtual PsInputState currentState() const = 0;

    /* 可选：详细状态给调试面板；不实现时宿主只用 current_state。 */
    virtual bool statusJson(std::string &out)
    {
        out = "{}";
        return true;
    }

    /* 可选：有限文件导入。文件插件实现它；流插件不实现。 */
    virtual bool importFile(std::string_view path, std::string_view opts, void **dataset, PsError *err)
    {
        (void)path;
        (void)opts;
        (void)dataset;
        fillError(err, 1, "import_file is not supported");
        return false;
    }

    virtual bool saveConfig(std::string &out, PsError *err)
    {
        (void)err;
        out = "{}";
        return true;
    }
    virtual bool loadConfig(std::string_view json, PsError *err)
    {
        (void)json;
        (void)err;
        return true;
    }

    virtual PsInputDialog dialog() const { return PsInputDialog{nullptr, nullptr, nullptr}; }
    virtual const void *extension(std::string_view id) const
    {
        (void)id;
        return nullptr;
    }

    virtual bool sendCommand(std::string_view command, std::string &out_json, PsError *err)
    {
        (void)command;
        (void)out_json;
        fillError(err, 1, "send_command is not supported");
        return false;
    }
    virtual bool seek(double time_s, PsError *err)
    {
        (void)time_s;
        fillError(err, 1, "seek is not supported");
        return false;
    }

protected:
    const PsDataInputServices &services() const { return services_; }
    const PsHostApi *host() const { return services_.host; }
    const PsSourceSink *sink() const { return services_.sink; }

private:
    friend struct VtableBuilder;

    bool bindImpl(PsDataInputServices services, PsError *err)
    {
        services_ = services;
        return onBind(err);
    }

    virtual bool onBind(PsError *err)
    {
        (void)err;
        return true;
    }

    PsDataInputServices services_{};
    std::string save_buf_;
    std::string command_buf_;
};

struct VtableBuilder
{
    template <typename T>
    static void *createT() noexcept
    {
        try {
            return new T();
        } catch (...) {
            return nullptr;
        }
    }

    template <typename T>
    static void destroyT(void *ctx) noexcept
    {
        delete static_cast<T *>(ctx);
    }

    template <typename T>
    static uint64_t capabilitiesT(void *ctx) noexcept
    {
        try {
            return static_cast<T *>(ctx)->capabilities();
        } catch (...) {
            return 0;
        }
    }

    template <typename T>
    static bool bindT(void *ctx, PsDataInputServices services, PsError *err) noexcept
    {
        try {
            return static_cast<T *>(ctx)->bindImpl(services, err);
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in bind");
            return false;
        }
    }

    template <typename T>
    static bool saveConfigT(void *ctx, PsStringView *out, PsError *err) noexcept
    {
        try {
            auto *self = static_cast<T *>(ctx);
            if (!self->saveConfig(self->save_buf_, err))
                return false;
            *out = viewOf(self->save_buf_);
            return true;
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in save_config");
            return false;
        }
    }

    template <typename T>
    static bool loadConfigT(void *ctx, PsStringView json, PsError *err) noexcept
    {
        try {
            return static_cast<T *>(ctx)->loadConfig(stringViewOf(json), err);
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in load_config");
            return false;
        }
    }

    template <typename T>
    static bool startT(void *ctx, PsError *err) noexcept
    {
        try {
            return static_cast<T *>(ctx)->start(err);
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in start");
            return false;
        }
    }

    template <typename T>
    static void stopT(void *ctx) noexcept
    {
        try {
            static_cast<T *>(ctx)->stop();
        } catch (...) {
        }
    }

    template <typename T>
    static bool pauseT(void *ctx, PsError *err) noexcept
    {
        try {
            return static_cast<T *>(ctx)->pause(err);
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in pause");
            return false;
        }
    }

    template <typename T>
    static bool resumeT(void *ctx, PsError *err) noexcept
    {
        try {
            return static_cast<T *>(ctx)->resume(err);
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in resume");
            return false;
        }
    }

    template <typename T>
    static bool pollT(void *ctx, PsError *err) noexcept
    {
        try {
            return static_cast<T *>(ctx)->poll(err);
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in poll");
            return false;
        }
    }

    template <typename T>
    static PsInputState stateT(void *ctx) noexcept
    {
        try {
            return static_cast<T *>(ctx)->currentState();
        } catch (...) {
            return PS_INPUT_ERROR;
        }
    }

    template <typename T>
    static PsInputDialog dialogT(void *ctx) noexcept
    {
        try {
            return static_cast<T *>(ctx)->dialog();
        } catch (...) {
            return PsInputDialog{nullptr, nullptr, nullptr};
        }
    }

    template <typename T>
    static const void *extensionT(void *ctx, PsStringView id) noexcept
    {
        try {
            return static_cast<T *>(ctx)->extension(stringViewOf(id));
        } catch (...) {
            return nullptr;
        }
    }

    template <typename T>
    static bool sendCommandT(void *ctx, PsStringView command, PsStringView *out,
                             PsError *err) noexcept
    {
        try {
            auto *self = static_cast<T *>(ctx);
            if (!self->sendCommand(stringViewOf(command), self->command_buf_, err))
                return false;
            *out = viewOf(self->command_buf_);
            return true;
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in send_command");
            return false;
        }
    }

    template <typename T>
    static bool seekT(void *ctx, double time_s, PsError *err) noexcept
    {
        try {
            return static_cast<T *>(ctx)->seek(time_s, err);
        } catch (const std::exception &e) {
            fillError(err, 1, e.what());
            return false;
        } catch (...) {
            fillError(err, 1, "unknown exception in seek");
            return false;
        }
    }

    template <typename T>
    static int32_t statusJsonT(void *ctx, char *out, uint32_t cap) noexcept
    {
        try {
            auto *self = static_cast<T *>(ctx);
            std::string s;
            if (!self->statusJson(s))
                return -1;
            if (!out || cap == 0 || s.size() + 1 > cap)
                return -1;
            std::memcpy(out, s.c_str(), s.size() + 1);
            return static_cast<int32_t>(s.size());
        } catch (...) {
            return -1;
        }
    }

    template <typename T>
    static int32_t importFileT(void *ctx, const char *path, const char *opts, void **dataset,
                               char *err, uint32_t errCap) noexcept
    {
        try {
            auto *self = static_cast<T *>(ctx);
            PsError e{};
            const bool ok = self->importFile(path ? path : "", opts ? opts : "",
                                             dataset, &e);
            if (!ok) {
                if (err && errCap) {
                    const size_t n = std::min<size_t>(sizeof(e.message) - 1, errCap - 1);
                    std::memcpy(err, e.message, n);
                    err[n] = '\0';
                }
                return e.code ? e.code : 1;
            }
            return 0;
        } catch (const std::exception &ex) {
            if (err && errCap)
                std::snprintf(err, errCap, "%s", ex.what());
            return 1;
        } catch (...) {
            if (err && errCap)
                std::snprintf(err, errCap, "unknown exception in import_file");
            return 1;
        }
    }

    template <typename T>
    static PsDataInputVtable make(const char *manifest)
    {
        return PsDataInputVtable{
            PS_DATA_INPUT_PROTOCOL_VERSION,
            sizeof(PsDataInputVtable),
            &createT<T>,
            &destroyT<T>,
            manifest,
            &capabilitiesT<T>,
            &bindT<T>,
            &saveConfigT<T>,
            &loadConfigT<T>,
            &startT<T>,
            &stopT<T>,
            &pauseT<T>,
            &resumeT<T>,
            &pollT<T>,
            &stateT<T>,
            &dialogT<T>,
            &extensionT<T>,
            &sendCommandT<T>,
            &seekT<T>,
            &statusJsonT<T>,
            &importFileT<T>,
        };
    }
};

} // namespace ps

/* 在插件 .cpp 末尾使用。 */
#define PS_DATA_INPUT_PLUGIN(ClassName, manifest)                                      \
    extern "C" const PsDataInputVtable *ps_get_data_input_vtable()                     \
    {                                                                                   \
        static const PsDataInputVtable vt = ::ps::VtableBuilder::make<ClassName>(manifest); \
        return &vt;                                                                     \
    }

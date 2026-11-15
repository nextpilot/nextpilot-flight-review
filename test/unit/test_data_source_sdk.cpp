#include "PsDataSourceBase.hpp"

#include <cassert>
#include <cstring>

namespace {

class DemoInput final : public ps::DataSourceBase
{
public:
    uint64_t capabilities() const override
    {
        return PS_INPUT_FILE | PS_INPUT_STREAM | PS_INPUT_DIALOG | PS_INPUT_COMMAND;
    }

    bool start(PsError *err) override
    {
        (void)err;
        started_ = true;
        state_ = PS_INPUT_RUNNING;
        return true;
    }

    void stop() override
    {
        started_ = false;
        state_ = PS_INPUT_IDLE;
    }

    bool poll(PsError *err) override
    {
        (void)err;
        ++polls_;
        if (polls_ >= 2)
            state_ = PS_INPUT_DONE;
        return true;
    }

    PsInputState currentState() const override { return state_; }

    bool loadConfig(std::string_view json, PsError *err) override
    {
        (void)err;
        config_.assign(json);
        return true;
    }

    bool saveConfig(std::string &out, PsError *err) override
    {
        (void)err;
        out = config_;
        return true;
    }

    PsInputDialog dialog() const override
    {
        return PsInputDialog{"demo.ui", R"({"type":"object"})"};
    }

    bool sendCommand(std::string_view command, std::string &out_json, PsError *err) override
    {
        (void)err;
        out_json = std::string("{\"echo\":\"") + std::string(command) + "\"}";
        return true;
    }

private:
    bool started_ = false;
    int polls_ = 0;
    PsInputState state_ = PS_INPUT_IDLE;
    std::string config_;
};

} // namespace

PS_DATA_SOURCE_EXTENSION(DemoInput, R"({"id":"demo_input","name":"Demo Input","version":"1.0.0","file_extensions":[".demo"]})")

int main()
{
    const PsDataSourceVtable *vt = ps_get_data_source_vtable();
    assert(vt);
    assert(vt->protocol_version == PS_DATA_SOURCE_PROTOCOL_VERSION);
    assert(vt->struct_size == sizeof(PsDataSourceVtable));
    assert(vt->manifest_json && std::strstr(vt->manifest_json, "demo_input"));

    void *ctx = vt->create();
    assert(ctx);
    assert(vt->capabilities(ctx) == (PS_INPUT_FILE | PS_INPUT_STREAM | PS_INPUT_DIALOG | PS_INPUT_COMMAND));

    PsDataSourceServices services{};
    services.abi_version = PS_HOST_API_VERSION;
    PsError err{};
    assert(vt->bind(ctx, services, &err));

    const char config[] = R"({"filepath":"flight.mav"})";
    PsStringView cfg{config, static_cast<uint32_t>(std::strlen(config))};
    assert(vt->load_config(ctx, cfg, &err));

    PsStringView saved{};
    assert(vt->save_config(ctx, &saved, &err));
    assert(std::string_view(saved.data, saved.size) == config);

    assert(vt->start(ctx, &err));
    assert(vt->current_state(ctx) == PS_INPUT_RUNNING);
    assert(vt->poll(ctx, &err));
    assert(vt->poll(ctx, &err));
    assert(vt->current_state(ctx) == PS_INPUT_DONE);

    PsInputDialog dlg = vt->get_dialog(ctx);
    assert(dlg.ui_path && std::strstr(dlg.ui_path, "demo.ui"));

    const char cmd[] = R"({"cmd":"version"})";
    PsStringView out{};
    assert(vt->send_command(ctx, PsStringView{cmd, static_cast<uint32_t>(std::strlen(cmd))},
                            &out, &err));
    assert(std::string_view(out.data, out.size).find("version") != std::string_view::npos);

    vt->stop(ctx);
    assert(vt->current_state(ctx) == PS_INPUT_IDLE);
    vt->destroy(ctx);
    return 0;
}

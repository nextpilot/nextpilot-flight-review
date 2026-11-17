#include "support/MemoryDataset.hpp"
#include "ps_kernel.h"

#include <cassert>
#include <cmath>
#include <cstring>

int main()
{
    ps::test::MemoryDataset mem;
    mem.add_table("imu");
    mem.add_column("imu", {"roll", "deg", {10.0, 0.0, 30.0}, {false, true, false}});
    mem.append_frame("imu", {0, 1000000, 2000000}, {{"roll", {10.0, 20.0, 30.0}}},
                     {{false, true, false}});

    PsDataset *ds = ps_dataset_new();
    assert(ds);
    char err[128] = {0};
    int64_t t[3] = {0, 1000000, 2000000};
    double v[3] = {10.0, 20.0, 30.0};
    unsigned char nulls[1] = {0x02};
    assert(ps_dataset_add_table_typed(ds, "imu", t, 3, err, sizeof(err)) == 0);
    assert(ps_dataset_add_table_column_typed(ds, "imu", "roll", "deg", PS_VALUE_F64, v, nulls, 3,
                                             err, sizeof(err)) == 0);

    const auto &table = mem.table("imu");
    assert(table.time_us.size() == ps_dataset_table_rows(ds, "imu"));
    assert(table.seq[2] == ps_dataset_seq_at(ds, "imu", 2));
    assert(std::fabs(mem.value_at("imu", "roll", 0) - 10.0) < 1e-12);
    assert(mem.is_null("imu", "roll", 1));
    const PsChannelInfo *ch = ps_dataset_channel(ds, 0);
    assert(ch && ps_dataset_seq_at(ds, "imu", 1) == 1);
    assert(std::isnan(ps_channel_value_at(ch, 1)));
    assert(ps_channel_value_at(ch, 2) == 30.0);
    ps_dataset_free(ds);
    return 0;
}

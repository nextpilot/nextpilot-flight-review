// 分析类算子的 C ABI 契约测试：值正确性 + 缓冲区语义。
#include "ps_kernel.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

static const double PI = 3.14159265358979323846;

static std::vector<double> sine(size_t n, double f, double fs, double amp = 1.0)
{
    std::vector<double> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = amp * std::sin(2.0 * PI * f * static_cast<double>(i) / fs);
    return v;
}

static double rmsFrom(const std::vector<double> &v, size_t from)
{
    double s = 0.0;
    for (size_t i = from; i < v.size(); ++i)
        s += v[i] * v[i];
    return std::sqrt(s / static_cast<double>(v.size() - from));
}

int main()
{
    char err[256] = {0};

    // ---- stats ----
    {
        std::vector<double> v = {1.0, 2.0, 3.0, 4.0};
        PsStats st{};
        assert(ps_stats_series(v.data(), 4, 1, &st, err, sizeof(err)) == 0);
        assert(st.count == 4);
        assert(std::fabs(st.mean - 2.5) < 1e-12);
        assert(std::fabs(st.median - 2.5) < 1e-12);
        assert(std::fabs(st.min - 1.0) < 1e-12);
        assert(std::fabs(st.max - 4.0) < 1e-12);
        // 不请求中位数时应为 NaN
        PsStats st2{};
        assert(ps_stats_series(v.data(), 4, 0, &st2, err, sizeof(err)) == 0);
        assert(std::isnan(st2.median));
        std::printf("stats: mean %.3f std %.3f rms %.3f\n", st.mean, st.std_dev, st.rms);
    }

    // ---- fft：整数周期，峰值幅度应等于输入幅值 ----
    {
        const size_t n = 1024;
        const double fs = 1024.0;
        const double f = 64.0;
        auto v = sine(n, f, fs, 2.0);
        std::vector<double> freq(n / 2 + 1);
        std::vector<double> mag(n / 2 + 1);
        uint32_t m = 0;
        assert(ps_fft_series(v.data(), static_cast<uint32_t>(n), fs, 0, freq.data(), mag.data(),
                             static_cast<uint32_t>(freq.size()), &m, err, sizeof(err)) == 0);
        assert(m == freq.size());
        size_t best = 0;
        for (size_t k = 1; k < m; ++k)
            if (mag[k] > mag[best])
                best = k;
        assert(std::fabs(freq[best] - f) < 1.0);
        assert(std::fabs(mag[best] - 2.0) < 0.05);
        std::printf("fft: peak %.1f Hz, 幅值 %.3f\n", freq[best], mag[best]);
    }

    // ---- psd：峰值频率 ----
    {
        const size_t n = 4096;
        const double fs = 1000.0;
        const double f = 100.0;
        auto v = sine(n, f, fs, 2.0);
        std::vector<double> freq(513);
        std::vector<double> psd(513);
        uint32_t m = 0;
        assert(ps_psd_series(v.data(), static_cast<uint32_t>(n), fs, 1024, 0.5, 1, freq.data(),
                             psd.data(), static_cast<uint32_t>(freq.size()), &m, err, sizeof(err)) ==
               0);
        size_t best = 1;
        for (size_t k = 1; k < m; ++k)
            if (psd[k] > psd[best])
                best = k;
        assert(std::fabs(freq[best] - f) < 2.0);
        assert(psd[best] > 0.0);
        std::printf("psd: peak %.1f Hz, %.3f\n", freq[best], psd[best]);
    }

    // ---- iir：低通压掉高频 ----
    {
        const size_t n = 4000;
        const double fs = 1000.0;
        auto pass = sine(n, 10.0, fs);
        auto stop = sine(n, 300.0, fs);
        std::vector<double> yp(n), ys(n);
        uint32_t on = 0;
        assert(ps_iir_series(pass.data(), static_cast<uint32_t>(n), fs, 50.0, 4, 0, 0.1,
                             yp.data(), static_cast<uint32_t>(n), &on, err, sizeof(err)) == 0);
        assert(on == n);
        assert(ps_iir_series(stop.data(), static_cast<uint32_t>(n), fs, 50.0, 4, 0, 0.1,
                             ys.data(), static_cast<uint32_t>(n), &on, err, sizeof(err)) == 0);
        const double rp = rmsFrom(yp, n / 2);
        const double rs = rmsFrom(ys, n / 2);
        assert(std::fabs(rp - 0.707) < 0.05);
        assert(rs / rp < 0.01);
        std::printf("iir: pass rms %.3f, stop/pass %.4f\n", rp, rs / rp);
    }

    // ---- decimate：长度与抗混叠 ----
    {
        const size_t n = 4000;
        const double fs = 1000.0;
        std::vector<double> t(n);
        for (size_t i = 0; i < n; ++i)
            t[i] = static_cast<double>(i) / fs;
        auto v = sine(n, 5.0, fs);
        std::vector<double> ot(n), ov(n);
        uint32_t on = 0;
        assert(ps_decimate_series(t.data(), v.data(), static_cast<uint32_t>(n), 8, ot.data(),
                                  ov.data(), static_cast<uint32_t>(n), &on, err, sizeof(err)) == 0);
        assert(on >= n / 8);
        assert(on < n);
        ot.resize(on);
        ov.resize(on);
        assert(std::fabs(rmsFrom(ov, on / 2) - 0.707) < 0.1);
        std::printf("decimate: %u 点, rms %.3f\n", on, rmsFrom(ov, on / 2));

        // 400 Hz 在 500 Hz 采样下会混叠，低通必须压掉
        auto alias = sine(n, 400.0, fs);
        assert(ps_decimate_series(t.data(), alias.data(), static_cast<uint32_t>(n), 4, ot.data(),
                                  ov.data(), static_cast<uint32_t>(n), &on, err, sizeof(err)) == 0);
        ov.resize(on);
        assert(rmsFrom(ov, on / 2) < 0.05);
    }

    // ---- 缓冲区语义：cap 不足返回 6，且回填实际长度 ----
    {
        const size_t n = 256;
        auto v = sine(n, 10.0, 256.0);
        std::vector<double> freq(4), mag(4);
        uint32_t m = 0;
        const int rc = ps_fft_series(v.data(), static_cast<uint32_t>(n), 256.0, 0, freq.data(),
                                     mag.data(), 4, &m, err, sizeof(err));
        assert(rc == 6);
        assert(m == n / 2 + 1);
        std::printf("cap 不足: rc=6, 实际长度 %u\n", m);
    }

    std::printf("kernel analysis ops ok\n");
    return 0;
}

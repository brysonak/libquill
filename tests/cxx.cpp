#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{

using Rows = std::vector<std::vector<std::string>>;

void take(Rows &rows, const quill_field &f, bool &open)
{
    std::string tmp(f.len, '\0');
    size_t n = 0;
    const char *v = quill_value(&f, &tmp[0], &n);
    if (!open)
        rows.emplace_back();
    rows.back().emplace_back(v, n);
    open = !f.eol;
}

Rows whole(const std::string &csv)
{
    auto p = std::unique_ptr<quill_parser>(new quill_parser);
    Rows rows;
    quill_field f;
    bool open = false;
    quill_init(p.get(), ',', QUILL_STRICT);
    quill_feed(p.get(), csv.data(), csv.size(), 1);
    while (quill_next(p.get(), &f) == QUILL_FIELD)
        take(rows, f, open);
    return rows;
}

Rows streamed(const std::string &csv, size_t step)
{
    auto p = std::unique_ptr<quill_parser>(new quill_parser);
    std::string buf;
    Rows rows;
    quill_field f;
    bool open = false;
    size_t pos = 0;
    int rc;
    quill_init(p.get(), ',', 0);
    for (;;) {
        size_t take_n = std::min(step, csv.size() - pos);
        buf.append(csv, pos, take_n);
        pos += take_n;
        quill_feed(p.get(), buf.data(), buf.size(), pos == csv.size());
        while ((rc = quill_next(p.get(), &f)) == QUILL_FIELD)
            take(rows, f, open);
        if (rc != QUILL_MORE)
            break;
        buf.erase(0, quill_consumed(p.get()));
    }
    return rows;
}

Rows guessed(const std::string &csv, size_t n)
{
    std::vector<quill_range> ranges(n);
    std::vector<Rows> parts(n);
    for (size_t k = 0; k < n; k++) {
        ranges[k].start = csv.size() / n * k;
        ranges[k].end = k + 1 == n ? csv.size() : csv.size() / n * (k + 1);
    }
    for (size_t k = 0; k < n; k++) {
        auto p = std::unique_ptr<quill_parser>(new quill_parser);
        quill_field f;
        bool open = false;
        quill_init_guess(p.get(), ',', 0, csv.data(), csv.size(), ranges.data(), k);
        while (quill_next(p.get(), &f) == QUILL_FIELD)
            take(parts[k], f, open);
    }
    Rows rows;
    for (size_t k = 0; k < n; k++) {
        if (!quill_guess_ok(ranges.data(), k)) {
            auto p = std::unique_ptr<quill_parser>(new quill_parser);
            quill_field f;
            bool open = false;
            parts[k].clear();
            quill_init_range(p.get(), ',', 0, csv.data(), csv.size(), ranges.data(), k);
            while (quill_next(p.get(), &f) == QUILL_FIELD)
                take(parts[k], f, open);
        }
        rows.insert(rows.end(), parts[k].begin(), parts[k].end());
    }
    return rows;
}

} // namespace

int main()
{
    std::string csv = "\xEF\xBB\xBFname,note\r\n";
    for (int i = 0; i < 2000; i++)
        csv += "Debbie" + std::to_string(i) + ",\"says \"\"hi\"\",\nthen leaves\"\r\n";
    csv += "last,";

    const Rows want = whole(csv);
    if (want.size() != 2002 || want[1][1] != "says \"hi\",\nthen leaves" || want.back().size() != 2
        || !want.back()[1].empty()) {
        std::fprintf(stderr, "whole parse wrong\n");
        return 1;
    }
    for (size_t step : {1u, 7u, 64u, 4096u}) {
        if (streamed(csv, step) != want) {
            std::fprintf(stderr, "streaming by %zu differs\n", step);
            return 1;
        }
    }
    for (size_t n : {1u, 3u, 16u, 200u}) {
        if (guessed(csv, n) != want) {
            std::fprintf(stderr, "%zu guessed ranges differ\n", n);
            return 1;
        }
    }
    std::printf("ok\n");
    return 0;
}
#define QUILL_IMPLEMENTATION
#include "quill.h"

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
    std::printf("ok\n");
    return 0;
}
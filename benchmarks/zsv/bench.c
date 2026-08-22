#include <zsv.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct zsv_opts o;
    unsigned long long rows = 0, fields = 0, bytes = 0, matches = 0;
    int scan, find;
    const char *name;
    size_t name_len;
    FILE *f;
    zsv_parser p;
    if (!(f = fopen(argv[2], "rb")))
        return 1;
    scan = strcmp(argv[1], "count") != 0;
    find = !strcmp(argv[1], "find");
    name = argc > 3 ? argv[3] : "";
    name_len = strlen(name);
    memset(&o, 0, sizeof o);
    o.stream = f;
    p = zsv_new(&o);
    while (zsv_next_row(p) == zsv_status_row) {
        size_t n = zsv_cell_count(p), i;
        rows++;
        if (!scan)
            continue;
        fields += n;
        for (i = 0; i < n; i++) {
            struct zsv_cell c = zsv_get_cell(p, i);
            bytes += c.len;
            matches += find && c.len == name_len && !memcmp(c.str, name, name_len);
        }
    }
    zsv_delete(p);
    fclose(f);
    if (!scan)
        printf("rows %llu\n", rows);
    else if (!find)
        printf("rows %llu fields %llu bytes %llu\n", rows, fields, bytes);
    else
        printf("matches %llu\n", matches);
    return 0;
}

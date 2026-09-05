#include <cisv/parser.h>

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    size_t name_len;
    unsigned long long rows, fields, bytes, matches;
} tally;

static void on_field(void *u, const char *s, size_t n)
{
    tally *t = (tally *)u;
    t->fields++;
    t->bytes += n;
    t->matches += n == t->name_len && !memcmp(s, t->name, n);
}

static void on_row(void *u)
{
    ((tally *)u)->rows++;
}

int main(int argc, char **argv)
{
    tally t = {"", 0, 0, 0, 0, 0};
    cisv_config cfg;
    cisv_parser *p;
    if (!strcmp(argv[1], "count")) {
        printf("rows %zu\n", cisv_parser_count_rows(argv[2]));
        return 0;
    }
    if (argc > 3)
        t.name = argv[3];
    t.name_len = strlen(t.name);
    cisv_config_init(&cfg);
    cfg.field_cb = on_field;
    cfg.row_cb = on_row;
    cfg.user = &t;
    p = cisv_parser_create_with_config(&cfg);
    if (cisv_parser_parse_file(p, argv[2]) < 0)
        return 1;
    cisv_parser_destroy(p);
    if (!strcmp(argv[1], "scan"))
        printf("rows %llu fields %llu bytes %llu\n", t.rows, t.fields, t.bytes);
    else
        printf("matches %llu\n", t.matches);
    return 0;
}

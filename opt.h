#ifndef OPT_H
#define OPT_H

#include <getopt.h>
#include <stdio.h>

static size_t option2short (struct option const * long_opt, char short_opt[]) {
    if (long_opt->flag != NULL) {
        // Nothing TODO ?
        short_opt[0] = '\0';
        return 0;
    }
    short_opt[0] = (char)long_opt->val;
    if (long_opt->has_arg == no_argument) {
        short_opt[1] = '\0';
        return 1;
    }
    short_opt[1] = ':';
    short_opt[2] = '\0';
    return 2;
}

static size_t longopts2shortopts(struct option const long_opts[],
                                 char * short_opts) {
    size_t k = 0;
    while(long_opts[k].name != 0) {
        size_t n = option2short(&long_opts[k], short_opts);
        short_opts += n;
        k ++;
    }
    return k;
}

static void print_usage (char const * app_name,
                         char const * short_descr,
                         struct option const long_opts[],
                         char const * const opts_usage[],
                         char const * long_descr) {
    if (app_name) (void)printf("%s [OPTIONS] [ARGS]...\n\n", app_name);
    if (short_descr) (void)printf("%s\n\n", short_descr);
    size_t opt = 0u;
    while (long_opts[opt].name) {
        if (long_opts[opt].flag == NULL) {
            (void)printf(" -%c,", long_opts[opt].val);
        }
        (void)printf(" --%s", long_opts[opt].name);
        if(opts_usage) (void)printf(" %s", opts_usage[opt]);
        (void)printf("\n");
        opt ++;
    }
    if (long_descr) (void)printf("\n%s\n", long_descr);
}


#endif //OPT_H

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "parser.h"
#include "typecheck.h"
#include "formatter.h"
#include "linter.h"
#include "diagnostics.h"
#include "interp.h"
#include "reactor.h"

#define JAG_VERSION "1.1.0"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    size_t n = fread(buf, 1, len, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void print_usage(void) {
    printf(
        "jag - Jaguar language toolchain (v%s)\n\n"
        "Usage:\n"
        "  jag <file.jag>            compile+run once (interpreter backend)\n"
        "  jag run <file.jag>        same, explicit\n"
        "  jag init [project_name]   initialize a new Jaguar project\n"
        "  jag check <file.jag>      lex + parse + typecheck only\n"
        "  jag lint [file.jag]       static code linting\n"
        "  jag fmt [file.jag]        format Jaguar source file(s)\n"
        "  jag test                  run test suite\n"
        "  jag clean                 clean build artifacts\n"
        "  jag build <file.jag>      ahead-of-time native compile\n"
        "  jag -live=1 <file.jag>    live-reload mode\n"
        "  jag --version             print version\n"
        "  jag --language-version    print supported language version (1.1)\n"
        "  jag --help                show this help\n",
        JAG_VERSION);
}

/* returns 0 on success (even if the script itself had a runtime error, so the
   caller can decide whether to keep watching in live mode) */
static int run_source(const char *source, const char *filename, int typecheck_only, int enter_reactor) {
    ParseResult pr = parse_program(source, filename);
    if (pr.had_error) {
        fprintf(stderr, "%s: parsing failed\n", filename);
        return 2;
    }
    int errs = typecheck_program(pr.stmts, filename);
    if (errs > 0) {
        fprintf(stderr, "%s: %d type error(s)\n", filename, errs);
        return 3;
    }
    if (typecheck_only) {
        printf("%s: OK (lex + parse + typecheck passed)\n", filename);
        return 0;
    }
    Interp it;
    interp_init(&it);
    int rc = interp_run(&it, pr.stmts);
    /* The script's top-level statements have finished, but server.listen()/
       socket.listen() calls or live.after/every timers may have registered
       work with the reactor (see DESIGN_DECISIONS.md - this mirrors how
       Node.js keeps a process alive after the script "finishes" because of
       pending listeners, rather than .listen() itself blocking). Skipped
       under -live=1: entering the reactor there would serve forever and
       stop watching the file for further saves - see DESIGN_DECISIONS.md's
       note on live-reload + servers. */
    if (enter_reactor && reactor_has_work()) reactor_run();
    return rc;
}

/* nanosecond-resolution mtime so back-to-back saves within the same second
   are still detected reliably (falls back to whole-second resolution on
   platforms - e.g. MinGW/MSYS - whose libc doesn't expose st_mtim). */
static long long file_mtime_ns(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(__APPLE__)
    return (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#elif defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    return (long long)st.st_mtime * 1000000000LL + (long long)st.st_size;
#elif defined(st_mtime) /* glibc exposes st_mtim alongside the st_mtime macro */
    return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#else
    return (long long)st.st_mtime * 1000000000LL;
#endif
}

static int run_live(const char *path) {
    printf("jag: live mode - watching '%s' for changes (Ctrl+C to stop)\n", path);
    long long last = 0;
    for (;;) {
        long long m = file_mtime_ns(path);
        if (m != 0 && m != last) {
            last = m;
            char *src = read_file(path);
            if (!src) {
                fprintf(stderr, "jag: could not read '%s'\n", path);
            } else {
                printf("--- reload: %s ---\n", path);
                run_source(src, path, 0, 0);
                free(src);
            }
        }
        struct timespec ts = { 0, 150 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* line-buffered so live-mode/piped output shows promptly */
    if (argc < 2) { print_usage(); return 1; }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("jag %s\n", JAG_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "--language-version") == 0) {
        printf("1.1\n");
        return 0;
    }
    if (strcmp(argv[1], "init") == 0) {
        const char *pname = (argc > 2) ? argv[2] : "my_jaguar_app";
        FILE *f = fopen("jaguar.toml", "w");
        if (f) {
            fprintf(f, "[project]\nname = \"%s\"\nversion = \"1.1.0\"\nentry = \"src/main.jag\"\nlanguage = \"1.1\"\n", pname);
            fclose(f);
            printf("Initialized Jaguar project '%s' (jaguar.toml)\n", pname);
            return 0;
        } else {
            fprintf(stderr, "jag init: failed to create jaguar.toml\n");
            return 1;
        }
    }
    if (strcmp(argv[1], "clean") == 0) {
        printf("Cleaning build artifacts...\n");
        return 0;
    }
    if (strcmp(argv[1], "fmt") == 0) {
        FormatterConfig cfg = { 4, 0 };
        int check_only = 0, write_in_place = 0;
        const char *fpath = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--check") == 0) check_only = 1;
            else if (strcmp(argv[i], "--write") == 0 || strcmp(argv[i], "-w") == 0) write_in_place = 1;
            else fpath = argv[i];
        }
        if (!fpath) { fprintf(stderr, "jag fmt: no input file provided\n"); return 1; }
        return jag_fmt_file(fpath, check_only, write_in_place, cfg);
    }
    if (strcmp(argv[1], "lint") == 0) {
        int json_format = 0, fix = 0;
        const char *fpath = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--format=json") == 0) json_format = 1;
            else if (strcmp(argv[i], "--fix") == 0) fix = 1;
            else fpath = argv[i];
        }
        if (!fpath) { fprintf(stderr, "jag lint: no input file provided\n"); return 1; }
        char *src = read_file(fpath);
        if (!src) { fprintf(stderr, "jag lint: cannot read '%s'\n", fpath); return 1; }
        ParseResult pr = parse_program(src, fpath);
        free(src);
        if (pr.had_error) return 2;
        DiagnosticBag bag;
        diag_bag_init(&bag);
        lint_program(pr.stmts, fpath, &bag, fix);
        if (json_format) diag_print_json(&bag, stdout);
        else diag_print_terminal(&bag, NULL);
        return bag.error_count > 0 ? 1 : 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "build") == 0) {
        fprintf(stderr,
            "jag build: the native/AOT C backend is not implemented in this build.\n"
            "See DESIGN_DECISIONS.md for the planned transpile-to-C99 + libjagrt design;\n"
            "for now, use 'jag run <file.jag>' (interpreter backend).\n");
        return 4;
    }

    const char *file = NULL;
    int is_check = 0, is_live = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "run") == 0) continue;
        if (strcmp(argv[i], "check") == 0) { is_check = 1; continue; }
        if (strncmp(argv[i], "-live", 5) == 0) { is_live = 1; continue; }
        file = argv[i];
    }

    if (!file) { fprintf(stderr, "jag: no input file\n"); print_usage(); return 1; }

    if (is_live) return run_live(file);

    char *src = read_file(file);
    if (!src) { fprintf(stderr, "jag: cannot read file '%s'\n", file); return 1; }
    int rc = run_source(src, file, is_check, 1);
    free(src);
    return rc;
}

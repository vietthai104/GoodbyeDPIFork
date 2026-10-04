/*
 * GoodbyeDPI TUI launcher
 *
 * A small menu in front of goodbyedpi.exe: pick a preset, DNS and a few
 * switches with the keyboard, then start. It only builds a command line
 * and runs goodbyedpi.exe from the same folder, nothing else.
 *
 * Needs Windows 10+ (ANSI escape sequences in the console).
 *
 *   goodbyedpi-tui.exe          interactive menu
 *   goodbyedpi-tui.exe --print  print the command line for the default
 *                               selection and exit (for scripts/testing)
 */
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#define ESC "\x1b"
#define CLEAR   ESC "[2J" ESC "[H"
#define REVERSE ESC "[7m"
#define DIM     ESC "[2m"
#define BOLD    ESC "[1m"
#define RESET   ESC "[0m"

enum item_e {
    ITEM_PRESET, ITEM_DNS, ITEM_QUIC, ITEM_VERBOSE, ITEM_START, ITEM_QUIT,
    ITEM_COUNT
};

/* Index 0 means "no --preset": goodbyedpi's default mode -9 is used instead. */
static const char *const preset_names[] = { "default", "viettel", "fpt", "vnpt", "steam" };
static const char *const preset_info[] = {
    "Mode -9 for any ISP, all traffic",
    "Viettel: mode -9 + DNS 1.1.1.1",
    "FPT Telecom: mode -5 (auto TTL) + DNS 8.8.8.8",
    "VNPT: mode -6 (wrong SEQ) + DNS 1.1.1.1",
    "Steam domains only, other traffic untouched. DNS 1.1.1.1",
};
#define PRESET_COUNT (sizeof(preset_names) / sizeof(preset_names[0]))

/* Index 0 means "whatever the preset says" (default mode: no DNS redirect). */
static const char *const dns_names[] = { "preset default", "1.1.1.1", "8.8.8.8", "9.9.9.9" };
static const char *const dns_addrs[] = { NULL,             "1.1.1.1", "8.8.8.8", "9.9.9.9" };
#define DNS_COUNT (sizeof(dns_names) / sizeof(dns_names[0]))

struct state {
    int selected;
    int preset;
    int dns;
    int quic;
    int verbose;
};

static void build_args(const struct state *st, char *out, size_t size) {
    int n;

    if (st->preset == 0)
        n = snprintf(out, size, "-9");
    else
        n = snprintf(out, size, "--preset %s", preset_names[st->preset]);

    if (n < 0 || (size_t)n >= size)
        return;
    if (dns_addrs[st->dns])
        n += snprintf(out + n, size - (size_t)n, " --dns-addr %s", dns_addrs[st->dns]);
    if (n >= 0 && (size_t)n < size && st->quic)
        n += snprintf(out + n, size - (size_t)n, " -q");
    if (n >= 0 && (size_t)n < size && st->verbose)
        snprintf(out + n, size - (size_t)n, " --dns-verb");
}

static void get_exe_path(char *out, size_t size) {
    char self[MAX_PATH];
    char *slash;

    out[0] = '\0';
    if (!GetModuleFileNameA(NULL, self, sizeof(self)))
        return;
    slash = strrchr(self, '\\');
    if (!slash)
        return;
    *slash = '\0';
    snprintf(out, size, "%s\\goodbyedpi.exe", self);
}

static void draw_row(int row, int selected, const char *label, const char *value,
                     int is_choice) {
    printf("  %s %-18s", row == selected ? REVERSE ">" : " ", label);
    if (is_choice && value)
        printf(" %s< %s >%s", row == selected ? "" : DIM, value, RESET);
    else if (value)
        printf(" %s", value);
    printf(RESET "\n");
}

static void draw(const struct state *st) {
    char args[256];

    build_args(st, args, sizeof(args));

    printf(CLEAR);
    printf(BOLD "  GoodbyeDPI" RESET "  -  Vietnam presets\n\n");

    draw_row(ITEM_PRESET, st->selected, "Preset", preset_names[st->preset], 1);
    printf("      " DIM "%s" RESET "\n", preset_info[st->preset]);
    draw_row(ITEM_DNS, st->selected, "DNS redirect", dns_names[st->dns], 1);
    draw_row(ITEM_QUIC, st->selected, "Block QUIC (-q)", st->quic ? "[x]" : "[ ]", 0);
    draw_row(ITEM_VERBOSE, st->selected, "Verbose DNS log", st->verbose ? "[x]" : "[ ]", 0);
    printf("\n");
    draw_row(ITEM_START, st->selected, "Start", NULL, 0);
    draw_row(ITEM_QUIT, st->selected, "Quit", NULL, 0);

    printf("\n  " DIM "Command:" RESET " goodbyedpi.exe %s\n", args);
    printf("\n  " DIM "Up/Down: move   Left/Right: change   Space/Enter: select   Q: quit" RESET "\n");
    printf("  " DIM "DNS redirect only affects plain UDP DNS, not DNS-over-HTTPS." RESET "\n");
}

static int wrap(int value, int delta, int count) {
    return (value + delta + count) % count;
}

static void change(struct state *st, int delta) {
    switch (st->selected) {
        case ITEM_PRESET:  st->preset = wrap(st->preset, delta, (int)PRESET_COUNT); break;
        case ITEM_DNS:     st->dns = wrap(st->dns, delta, (int)DNS_COUNT); break;
        case ITEM_QUIC:    st->quic = !st->quic; break;
        case ITEM_VERBOSE: st->verbose = !st->verbose; break;
        default: break;
    }
}

static BOOL WINAPI ignore_ctrl_c(DWORD type) {
    /* While goodbyedpi runs, Ctrl+C is meant for it, not for this launcher. */
    return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
}

static void pause_for_key(void) {
    printf("\nPress any key to return to the menu...");
    fflush(stdout);
    _getch();
}

static void run(const struct state *st) {
    char exe[MAX_PATH + 16];
    char args[256];
    char cmdline[MAX_PATH + 300];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 0;

    get_exe_path(exe, sizeof(exe));
    if (!exe[0] || GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) {
        printf(CLEAR "  goodbyedpi.exe was not found next to this program:\n  %s\n",
               exe[0] ? exe : "(unknown path)");
        pause_for_key();
        return;
    }

    build_args(st, args, sizeof(args));
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", exe, args);

    printf(CLEAR "  Running: %s\n  Press Ctrl+C to stop.\n\n", cmdline);
    fflush(stdout);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    SetConsoleCtrlHandler(ignore_ctrl_c, TRUE);
    if (CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        printf("\n  goodbyedpi.exe exited with code %lu\n", (unsigned long)code);
    } else {
        printf("\n  Could not start goodbyedpi.exe (error %lu)\n", (unsigned long)GetLastError());
    }
    SetConsoleCtrlHandler(ignore_ctrl_c, FALSE);
    pause_for_key();
}

int main(int argc, char *argv[]) {
    struct state st = { ITEM_START, 0, 0, 0, 0 };
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    if (argc > 1 && strcmp(argv[1], "--print") == 0) {
        char args[256];
        build_args(&st, args, sizeof(args));
        printf("goodbyedpi.exe %s\n", args);
        return 0;
    }

    if (!GetConsoleMode(out, &mode) ||
        !SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        fprintf(stderr, "This program needs a Windows 10+ console (ANSI escape support).\n");
        return 1;
    }

    printf(ESC "[?25l"); /* hide cursor */

    for (;;) {
        int key;

        draw(&st);
        key = _getch();

        if (key == 0 || key == 0xE0) {
            switch (_getch()) {
                case 72: st.selected = wrap(st.selected, -1, ITEM_COUNT); break; /* up */
                case 80: st.selected = wrap(st.selected, 1, ITEM_COUNT); break;  /* down */
                case 75: change(&st, -1); break;                                 /* left */
                case 77: change(&st, 1); break;                                  /* right */
                default: break;
            }
            continue;
        }

        switch (key) {
            case 'k': st.selected = wrap(st.selected, -1, ITEM_COUNT); break;
            case 'j': st.selected = wrap(st.selected, 1, ITEM_COUNT); break;
            case 'h': change(&st, -1); break;
            case 'l': change(&st, 1); break;
            case ' ':
            case '\r':
                if (st.selected == ITEM_START)
                    run(&st);
                else if (st.selected == ITEM_QUIT)
                    goto done;
                else
                    change(&st, 1);
                break;
            case 'q':
            case 'Q':
            case 27:
                goto done;
            default:
                break;
        }
    }

done:
    printf(ESC "[?25h" CLEAR);
    return 0;
}

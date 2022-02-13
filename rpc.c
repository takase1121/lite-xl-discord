#include "cross.h"

#define IPC_MAX_ID 9 // an arbitary number
#define BUF_SIZE 128
#define LENGTH(A) (sizeof(A) / sizeof(*(A)))

static char *envs[] = { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" };

typedef struct {
    HANDLE in, out;
} pipe_t;

DWORD pipe_thread(void *p) {
    pipe_t *pipe = (pipe_t *) p;
    DWORD success = 0, read = 0;
    char buf[BUF_SIZE];

    while (1) {
        success = ReadFile(pipe->in, buf, BUF_SIZE, &read, NULL);
        if (!success) break;

        success = WriteFile(pipe->out, buf, read, &read, NULL);
        if (!success) break;
    }

    CancelIo(GetStdHandle(STD_INPUT_HANDLE)); // force stdin to end immediately
    return 0;
}

int find_socket(HANDLE *handle, int pipe, char *prefix) {
    struct sockaddr_un addr;
    char path[PATH_MAX];
    char pathsep = '/';
    char *p = addr.sun_path;
    size_t max_path_len = sizeof(addr.sun_path) / sizeof(char);

    if (IsWindows()) {
        pathsep = '\\';
        max_path_len = PATH_MAX;
        p = path;
    }
    addr.sun_family = AF_UNIX;

    for (int i = 0; i <= IPC_MAX_ID; i++) {
        snprintf(p, max_path_len, "%s%cdiscord-ipc-%d", prefix, pathsep, i);

        if (IsWindows()) {
            *handle = CreateFileA(
                path,
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,
                0,
                (HANDLE) NULL
            );
            if (*handle != INVALID_HANDLE_VALUE) return 1;
        } else {
            if (connect(pipe, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) != -1)
                return 1;
        }
    }

    return 0;
}

#define PEXIT(E) (perror(E),exit(EXIT_FAILURE))

int main() {
    if (IsWindows()) {
        HANDLE pipe, h_in, h_out;
        if (!find_socket(&pipe, 0, "\\\\.\\pipe")
            || pipe == INVALID_HANDLE_VALUE)
            return EXIT_FAILURE;

        h_in = GetStdHandle(STD_INPUT_HANDLE);
        h_out = GetStdHandle(STD_OUTPUT_HANDLE);

        DWORD old_mode;
        GetConsoleMode(h_in, &old_mode);
        SetConsoleMode(h_in, old_mode & ~(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT));

        pipe_t pairs[2];
        pairs[0].in = h_in;
        pairs[0].out = pipe;

        pairs[1].in = pipe;
        pairs[1].out = h_out;

        // yes, NT2SYSV is a trampoline that translates stdcall to sysv
        HANDLE threads[2];
        threads[0] = CreateThread(NULL, 0, NT2SYSV(pipe_thread), &pairs[0], 0, NULL);
        threads[1] = CreateThread(NULL, 0, NT2SYSV(pipe_thread), &pairs[1], 0, NULL);

        WaitForMultipleObjectsEx(2, threads, 1, INFINITE, 1);
        CloseHandle(threads[0]);
        CloseHandle(threads[1]);

        CloseHandle(pipe);

        return 0;
    }

    int sock;
    if ((sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1)
        PEXIT("socket(): ");

    for (int i = 0; i < LENGTH(envs); i++) {
        char *env = getenv(envs[i]);
        if (env == NULL) continue;

        if (find_socket(NULL, sock, env))
            goto POLL_SOCK;
    }

    // try /tmp
    if (find_socket(NULL, sock, "/tmp"))
        goto POLL_SOCK;

    fprintf(stderr, "cannot find RPC socket\n");
    return EXIT_FAILURE;

POLL_SOCK:;
    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = sock, .events = POLLIN }
    };

    ssize_t length;
    char buffer[BUF_SIZE];
    while (poll(fds, 2, -1) >= 0) {
        if (fds[0].revents & POLLIN) {
            length = read(STDIN_FILENO, buffer, BUF_SIZE);
            if (send(sock, buffer, length, 0) == -1)
                PEXIT("send(): ");
        } else if (fds[1].revents & POLLIN) {
            length = recv(sock, buffer, BUF_SIZE, 0);
            if (length < 0)
                PEXIT("recv(): ");
            if (length == 0)
                break;

            write(STDOUT_FILENO, buffer, length);
        } else {
            PEXIT("unknown error: ");
        }
    }

    return 0;
}

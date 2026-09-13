#define _GNU_SOURCE
#include "platform/detach.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define CHILD_ENV "AUDIOSYNC_DETACHED_CHILD"
static volatile sig_atomic_t interrupted;

int detach_is_child(void) {
    return getenv(CHILD_ENV) != NULL;
}

static void silence(int signal) {
    (void)signal;
    int null = open("/dev/null", O_RDWR);
    if (null >= 0) {
        dup2(null, STDIN_FILENO);
        dup2(null, STDOUT_FILENO);
        dup2(null, STDERR_FILENO);
        if (null > STDERR_FILENO)
            close(null);
    }
}

void detach_prepare(void) {
    if (detach_is_child()) {
        signal(SIGUSR1, silence);
        sigset_t signal;
        sigemptyset(&signal);
        sigaddset(&signal, SIGUSR1);
        sigprocmask(SIG_UNBLOCK, &signal, NULL);
    }
}

static void interrupt(int signal) {
    (void)signal;
    interrupted = 1;
}

int detach_run(int argc, char **argv, int immediate) {
    (void)argc;
    sigset_t signal, previous;
    sigemptyset(&signal);
    sigaddset(&signal, SIGUSR1);
    sigprocmask(SIG_BLOCK, &signal, &previous);
    pid_t child = fork();
    if (child < 0) {
        sigprocmask(SIG_SETMASK, &previous, NULL);
        return -1;
    }
    if (!child) {
        setsid();
        setenv(CHILD_ENV, "1", 1);
        if (immediate)
            silence(0);
        execv("/proc/self/exe", argv);
        _exit(127);
    }
    sigprocmask(SIG_SETMASK, &previous, NULL);
    if (immediate)
        return 0;

    struct termios saved, raw;
    int terminal = tcgetattr(STDIN_FILENO, &saved) == 0;
    if (terminal) {
        raw = saved;
        raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    struct sigaction action = {0}, old_int, old_term;
    action.sa_handler = interrupt;
    sigaction(SIGINT, &action, &old_int);
    sigaction(SIGTERM, &action, &old_term);
    fputs("Press d to detach.\n", stderr);

    int status = 0;
    for (;;) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child)
            break;
        if (done < 0 && errno != EINTR) {
            status = -1;
            break;
        }
        char key;
        if (terminal && read(STDIN_FILENO, &key, 1) == 1 && (key == 'd' || key == 'D')) {
            kill(child, SIGUSR1);
            status = -2;
            break;
        }
        if (interrupted) {
            kill(child, SIGTERM);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR)
                ;
            break;
        }
        usleep(50000);
    }
    if (terminal)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    if (status == -2) {
        fputs("Detached. Use 'audiosync status -w' or 'audiosync logs -f' to reconnect.\n", stderr);
        return 0;
    }
    if (status < 0)
        return 1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

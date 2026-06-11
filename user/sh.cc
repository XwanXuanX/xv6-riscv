// Shell.

#include "user/user.h"
#include "kernel/fcntl.h"

#include <array>
#include <string_view>

// Parsed command representation
enum { EXEC = 1, REDIR = 2, PIPE = 3, LIST = 4, BACK = 5 };

#define MAXARGS 10

struct cmd {
    int type;
};

struct execcmd {
    int type;
    std::array<char *, MAXARGS> argv;
    std::array<char *, MAXARGS> eargv;
};

struct redircmd {
    int type;
    cmd *command;
    char *file;
    char *efile;
    int mode;
    int fd;
};

struct pipecmd {
    int type;
    cmd *left;
    cmd *right;
};

struct listcmd {
    int type;
    cmd *left;
    cmd *right;
};

struct backcmd {
    int type;
    cmd *command;
};

int fork1(); // Fork but panics on failure.
void panic(const char *);
cmd *parsecmd(char *);
void runcmd(cmd *) __attribute__((noreturn));

// Execute cmd.  Never returns.
void runcmd(cmd *cmd) {
    std::array<int, 2> p{};
    backcmd *bcmd;
    execcmd *ecmd;
    listcmd *lcmd;
    pipecmd *pcmd;
    redircmd *rcmd;

    if (cmd == nullptr) {
        exit(1);
    }

    switch (cmd->type) {
    default:
        panic("runcmd");

    case EXEC:
        ecmd = (execcmd *)cmd;
        if (ecmd->argv[0] == nullptr) {
            exit(1);
        }
        exec(ecmd->argv[0], (const char **)ecmd->argv.data());
        fprintf(2, "exec %s failed\n", ecmd->argv[0]);
        break;

    case REDIR:
        rcmd = (redircmd *)cmd;
        close(rcmd->fd);
        if (open(rcmd->file, rcmd->mode) < 0) {
            fprintf(2, "open %s failed\n", rcmd->file);
            exit(1);
        }
        runcmd(rcmd->command);
        break;

    case LIST:
        lcmd = (listcmd *)cmd;
        if (fork1() == 0) {
            runcmd(lcmd->left);
        }
        wait(nullptr);
        runcmd(lcmd->right);
        break;

    case PIPE:
        pcmd = (pipecmd *)cmd;
        if (pipe(p.data()) < 0) {
            panic("pipe");
        }
        if (fork1() == 0) {
            close(1);
            dup(p[1]);
            close(p[0]);
            close(p[1]);
            runcmd(pcmd->left);
        }
        if (fork1() == 0) {
            close(0);
            dup(p[0]);
            close(p[0]);
            close(p[1]);
            runcmd(pcmd->right);
        }
        close(p[0]);
        close(p[1]);
        wait(nullptr);
        wait(nullptr);
        break;

    case BACK:
        bcmd = (backcmd *)cmd;
        if (fork1() == 0) {
            runcmd(bcmd->command);
        }
        break;
    }
    exit(0);
}

int getcmd(char *buf, const int nbuf) {
    write(2, "$ ", 2);
    memset(buf, 0, nbuf);
    gets(buf, nbuf);
    if (buf[0] == 0) { // EOF
        return -1;
    }
    return 0;
}

int main() {
    static std::array<char, 100> buf{};
    int fd;

    // Ensure that three file descriptors are open.
    while ((fd = open("console", O_RDWR)) >= 0) {
        if (fd >= 3) {
            close(fd);
            break;
        }
    }

    // Read and run input commands.
    while (getcmd(buf.data(), sizeof(buf)) >= 0) {
        char *cmd = buf.data();
        while (*cmd == ' ' || *cmd == '\t') {
            cmd++;
        }
        if (*cmd == '\n') { // is a blank command
            continue;
        }
        if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
            // Chdir must be called by the parent, not the child.
            cmd[strlen(cmd) - 1] = 0; // chop \n
            if (chdir(cmd + 3) < 0) {
                fprintf(2, "cannot cd %s\n", cmd + 3);
            }
        } else {
            if (fork1() == 0) {
                runcmd(parsecmd(cmd));
            }
            wait(nullptr);
        }
    }
    exit(0);
}

void panic(const char *s) {
    fprintf(2, "%s\n", s);
    exit(1);
}

int fork1() {
    const int pid = fork();
    if (pid == -1) {
        panic("fork");
    }
    return pid;
}

// PAGEBREAK!
//  Constructors

cmd *exec_cmd() {
    const auto cmd = static_cast<struct execcmd *>(malloc(sizeof(execcmd)));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = EXEC;
    return reinterpret_cast<struct cmd *>(cmd);
}

cmd *redir_cmd(cmd *subcmd, char *file, char *efile, const int mode,
               const int fd) {
    const auto cmd = static_cast<struct redircmd *>(malloc(sizeof(redircmd)));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = REDIR;
    cmd->command = subcmd;
    cmd->file = file;
    cmd->efile = efile;
    cmd->mode = mode;
    cmd->fd = fd;
    return reinterpret_cast<struct cmd *>(cmd);
}

cmd *pipe_cmd(cmd *left, cmd *right) {
    const auto cmd = static_cast<pipecmd *>(malloc(sizeof(pipecmd)));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = PIPE;
    cmd->left = left;
    cmd->right = right;
    return reinterpret_cast<struct cmd *>(cmd);
}

cmd *list_cmd(cmd *left, cmd *right) {
    const auto cmd = static_cast<listcmd *>(malloc(sizeof(listcmd)));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;
    return reinterpret_cast<struct cmd *>(cmd);
}

cmd *back_cmd(cmd *subcmd) {
    const auto cmd = static_cast<backcmd *>(malloc(sizeof(backcmd)));
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = BACK;
    cmd->command = subcmd;
    return (struct cmd *)cmd;
}
// PAGEBREAK!
//  Parsing

constexpr std::string_view whitespace = " \t\r\n\v";
constexpr std::string_view symbols = "<|>&;()";

int gettoken(char **ps, const char *es, char **q, char **eq) {
    char *s = *ps;
    while (s < es && strchr(whitespace.data(), *s)) {
        s++;
    }
    if (q) {
        *q = s;
    }
    int ret = *s;
    switch (*s) {
    case 0:
        break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
        s++;
        break;
    case '>':
        s++;
        if (*s == '>') {
            ret = '+';
            s++;
        }
        break;
    default:
        ret = 'a';
        while (s < es && !strchr(whitespace.data(), *s) &&
               !strchr(symbols.data(), *s)) {
            s++;
        }
        break;
    }
    if (eq) {
        *eq = s;
    }

    while (s < es && strchr(whitespace.data(), *s)) {
        s++;
    }
    *ps = s;
    return ret;
}

int peek(char **ps, const char *es, const char *toks) {
    char *s = *ps;
    while (s < es && strchr(whitespace.data(), *s)) {
        s++;
    }
    *ps = s;
    return *s && strchr(toks, *s);
}

cmd *parseline(char **, char *);
cmd *parsepipe(char **, char *);
cmd *parseexec(char **, char *);
cmd *nulterminate(cmd *);

cmd *parsecmd(char *s) {
    char *es = s + strlen(s);
    cmd *cmd = parseline(&s, es);
    peek(&s, es, "");
    if (s != es) {
        fprintf(2, "leftovers: %s\n", s);
        panic("syntax");
    }
    nulterminate(cmd);
    return cmd;
}

cmd *parseline(char **ps, char *es) {
    cmd *cmd = parsepipe(ps, es);
    while (peek(ps, es, "&")) {
        gettoken(ps, es, nullptr, nullptr);
        cmd = back_cmd(cmd);
    }
    if (peek(ps, es, ";")) {
        gettoken(ps, es, nullptr, nullptr);
        cmd = list_cmd(cmd, parseline(ps, es));
    }
    return cmd;
}

cmd *parsepipe(char **ps, char *es) {
    cmd *cmd = parseexec(ps, es);
    if (peek(ps, es, "|")) {
        gettoken(ps, es, nullptr, nullptr);
        cmd = pipe_cmd(cmd, parsepipe(ps, es));
    }
    return cmd;
}

cmd *parseredirs(cmd *cmd, char **ps, const char *es) {
    char *q, *eq;

    while (peek(ps, es, "<>")) {
        const int tok = gettoken(ps, es, nullptr, nullptr);
        if (gettoken(ps, es, &q, &eq) != 'a') {
            panic("missing file for redirection");
        }
        switch (tok) {
        case '<':
            cmd = redir_cmd(cmd, q, eq, O_RDONLY, 0);
            break;
        case '>':
            cmd = redir_cmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
            break;
        case '+': // >>
            cmd = redir_cmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
            break;
        default:;
        }
    }
    return cmd;
}

cmd *parseblock(char **ps, char *es) {
    if (!peek(ps, es, "(")) {
        panic("parseblock");
    }
    gettoken(ps, es, nullptr, nullptr);
    cmd *cmd = parseline(ps, es);
    if (!peek(ps, es, ")")) {
        panic("syntax - missing )");
    }
    gettoken(ps, es, nullptr, nullptr);
    cmd = parseredirs(cmd, ps, es);
    return cmd;
}

cmd *parseexec(char **ps, char *es) {
    char *q, *eq;
    int tok;

    if (peek(ps, es, "(")) {
        return parseblock(ps, es);
    }

    cmd *ret = exec_cmd();
    const auto cmd = (struct execcmd *)ret;

    int argc = 0;
    ret = parseredirs(ret, ps, es);
    while (!peek(ps, es, "|)&;")) {
        if ((tok = gettoken(ps, es, &q, &eq)) == 0) {
            break;
        }
        if (tok != 'a') {
            panic("syntax");
        }
        cmd->argv[argc] = q;
        cmd->eargv[argc] = eq;
        argc++;
        if (argc >= MAXARGS) {
            panic("too many args");
        }
        ret = parseredirs(ret, ps, es);
    }
    cmd->argv[argc] = nullptr;
    cmd->eargv[argc] = nullptr;
    return ret;
}

// NUL-terminate all the counted strings.
cmd *nulterminate(cmd *cmd) {
    int i;
    backcmd *bcmd;
    execcmd *ecmd;
    listcmd *lcmd;
    pipecmd *pcmd;
    redircmd *rcmd;

    if (cmd == nullptr) {
        return nullptr;
    }

    switch (cmd->type) {
    case EXEC:
        ecmd = (execcmd *)cmd;
        for (i = 0; ecmd->argv[i]; i++) {
            *ecmd->eargv[i] = 0;
        }
        break;

    case REDIR:
        rcmd = (redircmd *)cmd;
        nulterminate(rcmd->command);
        *rcmd->efile = 0;
        break;

    case PIPE:
        pcmd = (pipecmd *)cmd;
        nulterminate(pcmd->left);
        nulterminate(pcmd->right);
        break;

    case LIST:
        lcmd = (listcmd *)cmd;
        nulterminate(lcmd->left);
        nulterminate(lcmd->right);
        break;

    case BACK:
        bcmd = (backcmd *)cmd;
        nulterminate(bcmd->command);
        break;
    default:;
    }
    return cmd;
}

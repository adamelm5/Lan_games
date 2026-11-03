#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <termios.h>
#include <signal.h>

#define PORT 8080
#define WIDTH 60

#define RED     "\033[1;31m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define CYAN    "\033[1;36m"
#define MAGENTA "\033[1;35m"
#define WHITE   "\033[1;37m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define RESET   "\033[0m"

static struct termios orig_termios;

static void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    printf("\033[?25h");   // s'assure que le curseur est visible
    fflush(stdout);
}

static void on_signal(int sig) {
    (void)sig;
    restore_terminal();
    _exit(0);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);   // pas de ligne tamponnée, pas d'écho
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

const char *timer_color(int remaining) {
    if (remaining > 20) return GREEN;
    if (remaining > 10) return YELLOW;
    return RED;
}

void print_border(const char *left, const char *right, const char *fill) {
    printf(WHITE "%s", left);
    for (int i = 0; i < WIDTH - 2; i++) printf("%s", fill);
    printf("%s" RESET "\n", right);
}

void print_centered(const char *color, const char *text) {
    int len = strlen(text), pad = (WIDTH - 2 - len) / 2;
    if (pad < 0) pad = 0;
    printf(WHITE "│" RESET);
    for (int i = 0; i < pad; i++) printf(" ");
    printf("%s%s%s", color, text, RESET);
    for (int i = pad + len; i < WIDTH - 2; i++) printf(" ");
    printf(WHITE "│" RESET "\n");
}

void print_timer_bar(int remaining, int total) {
    int bar_width = WIDTH - 20;
    int filled = (remaining * bar_width) / total;
    if (filled < 0) filled = 0; if (filled > bar_width) filled = bar_width;
    const char *col = timer_color(remaining);
    printf(WHITE "│" RESET " %s", col);
    for (int i = 0; i < filled; i++) printf("█");
    printf(DIM);
    for (int i = filled; i < bar_width; i++) printf("░");
    printf(RESET " %s%2d s" RESET, col, remaining);
    int used = 1 + bar_width + 1 + 4;
    for (int i = used; i < WIDTH - 2; i++) printf(" ");
    printf(WHITE "│" RESET "\n");
}

void render(char my_id, char state, int timer, int scoreA, int scoreB, int tryA, int tryB, const char *last_msg, const char *question, const char *input) {
    printf("\033[H"); 
    
    print_border("┌", "┐", "─");
    print_centered(MAGENTA BOLD, "JEU DES CAPITALES");
    print_border("├", "┤", "─");
    char line[256];
    snprintf(line, sizeof(line), "Vous etes : Joueur %c", (my_id == '0') ? '1' : '2');
    print_centered(CYAN, line);
    snprintf(line, sizeof(line), "Joueur 1 : %d pt%s | Joueur 2 : %d pt%s", scoreA, scoreA > 1 ? "s" : "", scoreB, scoreB > 1 ? "s" : "");
    print_centered(WHITE, line);
    print_border("├", "┤", "─");
    
    if (question[0]) {
        char qcopy[512]; strncpy(qcopy, question, sizeof(qcopy) - 1);
        int qlen = strlen(qcopy), maxw = WIDTH - 6, pos = 0;
        while (pos < qlen) {
            int chunk = qlen - pos; if (chunk > maxw) chunk = maxw;
            if (pos + chunk < qlen) {
                int cut = chunk;
                while (cut > 0 && qcopy[pos + cut] != ' ') cut--;
                if (cut > 0) chunk = cut;
            }
            char tmp[512]; strncpy(tmp, qcopy + pos, chunk); tmp[chunk] = 0;
            print_centered(WHITE BOLD, tmp);
            pos += chunk;
            while (qcopy[pos] == ' ') pos++;
        }
    } else print_centered(DIM, "");

    print_border("├", "┤", "─");
    if (state == 'Q' || state == 'E') print_timer_bar(timer, 40);
    else print_centered(DIM, "");

    int my_tries = (my_id == '0') ? tryA : tryB;
    int left = 2 - my_tries; if (left < 0) left = 0;
    snprintf(line, sizeof(line), "Tentatives restantes : %d / 2", left);
    print_centered(left > 0 ? YELLOW : RED, line);
    print_border("├", "┤", "─");

    if (last_msg[0]) {
        snprintf(line, sizeof(line), "%s", last_msg);
        if ((int)strlen(line) > WIDTH - 4) line[WIDTH - 4] = 0;
        print_centered(CYAN, line);
    } else print_centered(DIM, "");
    print_border("└", "┘", "─");

    if (state == 'E') {
        printf("\n" WHITE "Appuyez sur Entree pour quitter." RESET "\n\033[K");
    } else {
        printf("\n" GREEN BOLD "Entrez votre reponse : " RESET "%s\033[K", input);
    }
    fflush(stdout);
}

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons(PORT);
    
    char ip[64] = "127.0.0.1";
    FILE *f = fopen("ip.txt", "r");
    if (f) { if (fgets(ip, sizeof(ip), f)) ip[strcspn(ip, "\n")] = 0; fclose(f); }
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) { perror("inet_pton"); return 1; }

    printf("Connexion au serveur sur %s...\n", ip);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    printf("En attente de l'autre joueur...\n");
    char my_id = '?';
    if (recv(sock, &my_id, 1, 0) <= 0) return 0;

    char input[256] = ""; 
    int input_len = 0;
    char state = '?'; 
    int timer = 0, scoreA = 0, scoreB = 0, tryA = 0, tryB = 0;
    char last_msg[256] = "", question[512] = "";
    char old_question[512] = ""; 
    int last_timer = -1;

    printf("\033[2J"); // Efface l'écran une seule fois au tout début
    enable_raw_mode(); // ACTIVE LE MODE RAW ICI

    while (1) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(sock, &rfds); FD_SET(STDIN_FILENO, &rfds);
        if (select(sock + 1, &rfds, NULL, NULL, NULL) < 0) break;

        if (FD_ISSET(sock, &rfds)) {
            char buf[2048]; int n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;
            buf[n] = 0;

            char st, msg[256] = "", q[512] = ""; int t, sa, sb, ta, tb;
            char *pipe = strchr(buf, '|');
            if (pipe) { *pipe = 0; strncpy(q, pipe + 1, sizeof(q) - 1); q[strcspn(q, "\r\n")] = 0; }
            if (sscanf(buf, "S %c %d %d %d %d %d %255[^\n]", &st, &t, &sa, &sb, &ta, &tb, msg) >= 6) {
                state = st; timer = t; scoreA = sa; scoreB = sb; tryA = ta; tryB = tb;
                strncpy(last_msg, msg, sizeof(last_msg) - 1); strncpy(question, q, sizeof(question) - 1);
            }

            // Détection du changement de question
            if (strcmp(question, old_question) != 0) {
                input[0] = '\0';
                input_len = 0;
                strncpy(old_question, question, sizeof(old_question) - 1);
            }

            // Détection du temps écoulé
            if (timer == 0 && last_timer > 0) {
                input[0] = '\0';
                input_len = 0;
            }
            last_timer = timer;

            printf("\033[?25l"); 
            render(my_id, state, timer, scoreA, scoreB, tryA, tryB, last_msg, question, input); 
            printf("\033[?25h"); 

            if (state == 'E') { 
                char tmp; 
                while (read(STDIN_FILENO, &tmp, 1) > 0 && tmp != '\n') {} 
                break; 
            }
        }

        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char c; if (read(STDIN_FILENO, &c, 1) <= 0) break;
            
            if (c == '\n' || c == '\r') { // '\r' car en mode raw la touche entrée envoie \r
                input[input_len] = 0;
                if (input_len > 0) { 
                    char to_send[260]; snprintf(to_send, sizeof(to_send), "%s\n", input); 
                    send(sock, to_send, strlen(to_send), 0); 
                }
                input_len = 0; 
                input[0] = '\0';
                printf("\033[?25l"); 
                render(my_id, state, timer, scoreA, scoreB, tryA, tryB, last_msg, question, input); 
                printf("\033[?25h");
            } else if (c == 127 || c == 8) { // Backspace
                if (input_len > 0) { 
                    input_len--; 
                    input[input_len] = 0; 
                    printf("\r\033[K" GREEN BOLD "Entrez votre reponse : " RESET "%s", input); 
                    fflush(stdout); 
                }
            } else if (c >= 32 && c < 127 && input_len < (int)sizeof(input) - 1) {
                input[input_len++] = c; 
                input[input_len] = 0;
                printf("\r\033[K" GREEN BOLD "Entrez votre reponse : " RESET "%s", input); 
                fflush(stdout);
            }
        }
    }
    printf("\n"); close(sock); return 0;
}

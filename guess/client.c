#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT 8080
#define WIDTH 60

#define RED     "\033[1;31m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define CYAN    "\033[1;36m"
#define BLUE    "\033[1;34m"
#define MAGENTA "\033[1;35m"
#define WHITE   "\033[1;37m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define RESET   "\033[0m"

/* Couleur du timer selon le temps restant */
const char *timer_color(int remaining) {
    if (remaining > 20) return GREEN;
    if (remaining > 10) return YELLOW;
    return RED;
}

/* Construit une ligne de cadre : ┌───...───┐ etc. */
void print_border(const char *left, const char *right, const char *fill) {
    printf(WHITE "%s", left);
    for (int i = 0; i < WIDTH - 2; i++) printf("%s", fill);
    printf("%s" RESET "\n", right);
}

/* Affiche une ligne encadrée, avec padding pour occuper toute la largeur.
   Le contenu est censé faire <= WIDTH-2 caractères visibles (hors ANSI). */
void print_boxed(const char *content_visual_len_known, int vis_len,
                 const char *content) {
    printf(WHITE "│" RESET " %s", content);
    for (int i = vis_len + 1; i < WIDTH - 1; i++) printf(" ");
    printf(WHITE "│" RESET "\n");
}

/* Centre un texte dans une largeur donnée (visuelle, sans ANSI) */
void print_centered(const char *color, const char *text) {
    int len = strlen(text);
    int pad = (WIDTH - 2 - len) / 2;
    if (pad < 0) pad = 0;
    printf(WHITE "│" RESET);
    for (int i = 0; i < pad; i++) printf(" ");
    printf("%s%s%s", color, text, RESET);
    int used = pad + len;
    for (int i = used; i < WIDTH - 2; i++) printf(" ");
    printf(WHITE "│" RESET "\n");
}

/* Barre de progression du timer */
void print_timer_bar(int remaining, int total) {
    int bar_width = WIDTH - 20;
    int filled = (remaining * bar_width) / total;
    if (filled < 0) filled = 0;
    if (filled > bar_width) filled = bar_width;
    const char *col = timer_color(remaining);

    printf(WHITE "│" RESET " ");
    printf("%s", col);
    for (int i = 0; i < filled; i++) printf("█");
    printf(DIM);
    for (int i = filled; i < bar_width; i++) printf("░");
    printf(RESET);
    printf(" %s%2d s" RESET, col, remaining);
    // Padding restant
    int used = 1 + bar_width + 1 + 4; // espace + barre + espace + "NN s"
    for (int i = used; i < WIDTH - 2; i++) printf(" ");
    printf(WHITE "│" RESET "\n");
}

/* Réaffiche tout l'écran de jeu.
   input/input_len = saisie en cours (pour la réinjecter). */
void render(char my_id, char state, int timer, int scoreA, int scoreB,
            int tryA, int tryB, const char *last_msg,
            const char *question, const char *input) {
    /* Curseur en haut à gauche, effacement de chaque ligne au fur et à mesure
       (on n'utilise PAS \033[2J pour éviter le clignotement) */
    printf("\033[H");

    /* Ligne vide pour marge */
    printf("\033[K\n");

    /* Titre */
    print_border("┌", "┐", "─");
    print_centered(MAGENTA BOLD, "JEU DES CAPITALES");
    print_border("├", "┤", "─");

    /* Infos joueur */
    char line[256];
    snprintf(line, sizeof(line), "Vous êtes : Joueur %c", (my_id == '0') ? '1' : '2');
    print_centered(CYAN, line);

    snprintf(line, sizeof(line), "Joueur 1 : %d pt%s   |   Joueur 2 : %d pt%s",
             scoreA, scoreA > 1 ? "s" : "", scoreB, scoreB > 1 ? "s" : "");
    print_centered(WHITE, line);

    print_border("├", "┤", "─");

    /* Question, centrée, en couleur claire */
    if (question[0]) {
        /* Découpe la question en lignes de max WIDTH-6 caractères */
        char qcopy[512];
        strncpy(qcopy, question, sizeof(qcopy) - 1);
        qcopy[sizeof(qcopy) - 1] = 0;

        int qlen = strlen(qcopy);
        int maxw = WIDTH - 6;
        int pos = 0;
        int first = 1;
        while (pos < qlen) {
            int chunk = qlen - pos;
            if (chunk > maxw) chunk = maxw;
            /* Essaie de couper à un espace */
            if (pos + chunk < qlen) {
                int cut = chunk;
                while (cut > 0 && qcopy[pos + cut] != ' ') cut--;
                if (cut > 0) chunk = cut;
            }
            char tmp[512];
            strncpy(tmp, qcopy + pos, chunk);
            tmp[chunk] = 0;
            if (first) {
                print_centered(WHITE BOLD, tmp);
                first = 0;
            } else {
                print_centered(WHITE BOLD, tmp);
            }
            pos += chunk;
            while (qcopy[pos] == ' ') pos++;
        }
    } else {
        print_centered(DIM, "");
    }

    print_border("├", "┤", "─");

    /* Timer */
    if (state == 'Q' || state == 'E') {
        print_timer_bar(timer, 40);
    } else {
        print_centered(DIM, "");
    }

    /* Tentatives */
    int my_tries = (my_id == '0') ? tryA : tryB;
    int left = 2 - my_tries;
    if (left < 0) left = 0;
    snprintf(line, sizeof(line), "Tentatives restantes : %d / 2", left);
    print_centered(left > 0 ? YELLOW : RED, line);

    print_border("├", "┤", "─");

    /* Message du serveur (dernier événement) */
    if (last_msg[0]) {
        snprintf(line, sizeof(line), "%s", last_msg);
        /* Tronque si trop long */
        if ((int)strlen(line) > WIDTH - 4) line[WIDTH - 4] = 0;
        print_centered(CYAN, line);
    } else {
        print_centered(DIM, "");
    }

    print_border("└", "┘", "─");

    /* Zone de saisie */
    if (state == 'E') {
        printf("\n" WHITE "Appuyez sur Entrée pour quitter." RESET "\n");
    } else {
        printf("\n" GREEN BOLD "Entrez votre réponse : " RESET "%s", input);
        fflush(stdout);
        /* Place le curseur à la fin de la saisie :
           on est déjà à la bonne position car on vient d'écrire input */
    }
}

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        perror("inet_pton"); return 1;
    }

    printf("Connexion au serveur...\n");
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }

    printf("En attente de l'autre joueur...\n");

    char my_id = '?';
    if (recv(sock, &my_id, 1, 0) <= 0) { printf("Connexion perdue.\n"); return 0; }

    char input[256] = "";
    int input_len = 0;

    char state = '?';
    int timer = 0, scoreA = 0, scoreB = 0, tryA = 0, tryB = 0;
    char last_msg[256] = "";
    char question[512] = "";

    /* Premier effacement complet, une seule fois */
    printf("\033[2J");
    printf("\033[?25l"); /* cache le curseur pendant le rendu */

    int first_render = 1;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        FD_SET(STDIN_FILENO, &rfds);

        int sel = select(sock + 1, &rfds, NULL, NULL, NULL);
        if (sel < 0) { perror("select"); break; }

        if (FD_ISSET(sock, &rfds)) {
            char buf[2048];
            int n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                printf("\033[?25h\nConnexion perdue.\n");
                break;
            }
            buf[n] = 0;

            char st, msg[256] = "", q[512] = "";
            int t, sa, sb, ta, tb;
            char *pipe = strchr(buf, '|');
            if (pipe) {
                *pipe = 0;
                strncpy(q, pipe + 1, sizeof(q) - 1);
                q[strcspn(q, "\r\n")] = 0;
            }
            if (sscanf(buf, "S %c %d %d %d %d %d %255[^\n]",
                       &st, &t, &sa, &sb, &ta, &tb, msg) >= 6) {
                state = st; timer = t;
                scoreA = sa; scoreB = sb;
                tryA = ta; tryB = tb;
                strncpy(last_msg, msg, sizeof(last_msg) - 1);
                strncpy(question, q, sizeof(question) - 1);
            }

            /* Cache le curseur pendant le redessin, puis le remet */
            printf("\033[?25l");
            render(my_id, state, timer, scoreA, scoreB,
                   tryA, tryB, last_msg, question, input);
            printf("\033[?25h");
            fflush(stdout);
            first_render = 0;

            if (state == 'E') {
                /* Attend une touche puis quitte */
                char tmp;
                while (read(STDIN_FILENO, &tmp, 1) <= 0) {}
                if (tmp == '\n') break;
            }
        }

        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char c;
            if (read(STDIN_FILENO, &c, 1) <= 0) break;

            if (c == '\n') {
                input[input_len] = 0;
                if (input_len > 0) {
                    char to_send[260];
                    snprintf(to_send, sizeof(to_send), "%s\n", input);
                    send(sock, to_send, strlen(to_send), 0);
                }
                input_len = 0;
                input[0] = 0;
                /* Redessine pour vider le champ de saisie */
                printf("\033[?25l");
                render(my_id, state, timer, scoreA, scoreB,
                       tryA, tryB, last_msg, question, input);
                printf("\033[?25h");
                fflush(stdout);
            } else if (c == 127 || c == 8) {
                if (input_len > 0) {
                    input_len--;
                    input[input_len] = 0;
                    printf("\033[?25l");
                    render(my_id, state, timer, scoreA, scoreB,
                           tryA, tryB, last_msg, question, input);
                    printf("\033[?25h");
                    fflush(stdout);
                }
            } else if (c >= 32 && c < 127 && input_len < (int)sizeof(input) - 1) {
                input[input_len++] = c;
                input[input_len] = 0;
                /* Réécrit juste la ligne de saisie sans tout redessiner */
                printf("\r\033[K" GREEN BOLD "Entrez votre réponse : " RESET "%s", input);
                fflush(stdout);
            }
        }
        (void)first_render;
    }

    printf("\033[?25h");
    close(sock);
    return 0;
}
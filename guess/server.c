#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <ctype.h>
#include <time.h>

#define PORT 8080
#define MAX_ALL 2000
#define NB_PLAY 20
#define TIME_LIMIT 40
#define MAX_TRY 2

typedef struct {
    char question[512];
    char answer[256];
} QA;

QA all_questions[MAX_ALL];
int nb_all = 0;

QA game_questions[NB_PLAY];
int nb_game = 0;

/* Charge capitales.txt : ligne 2 = Q1, ligne 3 = R1, etc. La ligne 1 est ignorée. */
int load_questions(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen capitales.txt"); return -1; }

    char line[512];
    int lineno = 0;
    char pending_q[512] = {0};
    int have_q = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        line[strcspn(line, "\r\n")] = 0;
        if (lineno == 1) continue;

        if (lineno % 2 == 0) {
            strncpy(pending_q, line, sizeof(pending_q) - 1);
            pending_q[sizeof(pending_q) - 1] = 0;
            have_q = 1;
        } else {
            if (have_q && nb_all < MAX_ALL) {
                strncpy(all_questions[nb_all].question, pending_q,
                        sizeof(all_questions[nb_all].question) - 1);
                strncpy(all_questions[nb_all].answer, line,
                        sizeof(all_questions[nb_all].answer) - 1);
                nb_all++;
                have_q = 0;
            }
        }
    }
    fclose(f);
    return nb_all;
}

/* Tirage aléatoire sans répétition (Fisher-Yates) */
void pick_random_questions(void) {
    srand((unsigned)time(NULL));

    int *idx = malloc(nb_all * sizeof(int));
    if (!idx) { perror("malloc"); exit(1); }
    for (int i = 0; i < nb_all; i++) idx[i] = i;

    for (int i = nb_all - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
    }

    nb_game = (nb_all < NB_PLAY) ? nb_all : NB_PLAY;
    for (int i = 0; i < nb_game; i++) {
        game_questions[i] = all_questions[idx[i]];
    }
    free(idx);
}

/* Comparaison insensible à la casse et aux espaces de début/fin */
int answer_matches(const char *given, const char *expected) {
    while (*given == ' ' || *given == '\t') given++;
    char a[256];
    strncpy(a, given, sizeof(a) - 1);
    a[sizeof(a) - 1] = 0;
    int len = strlen(a);
    while (len > 0 && (a[len-1] == ' ' || a[len-1] == '\t')) a[--len] = 0;

    char b[256];
    strncpy(b, expected, sizeof(b) - 1);
    b[sizeof(b) - 1] = 0;
    len = strlen(b);
    while (len > 0 && (b[len-1] == ' ' || b[len-1] == '\t')) b[--len] = 0;

    for (int i = 0; a[i]; i++) a[i] = tolower((unsigned char)a[i]);
    for (int i = 0; b[i]; i++) b[i] = tolower((unsigned char)b[i]);
    return strcmp(a, b) == 0;
}

/* Envoie l'état complet au client.
   Format : "S <state> <timer> <scoreA> <scoreB> <tryA> <tryB> <msg>|<question>\n" */
void send_state(int sock, char state, int timer, int scoreA, int scoreB,
                int tryA, int tryB, const char *msg, const char *question) {
    char buf[2048];
    snprintf(buf, sizeof(buf), "S %c %d %d %d %d %d %s|%s\n",
             state, timer, scoreA, scoreB, tryA, tryB,
             msg ? msg : "", question ? question : "");
    send(sock, buf, strlen(buf), 0);
}

/* Lit une ligne complète depuis un socket (jusqu'à \n) */
int read_line(int sock, char *buf, int maxlen) {
    int i = 0;
    while (i < maxlen - 1) {
        char c;
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return n;
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = 0;
    return 1;
}

int main(void) {
    if (load_questions("capitales.txt") < 0) return 1;
    if (nb_all == 0) { fprintf(stderr, "Aucune question chargée.\n"); return 1; }
    printf("Chargé %d questions depuis capitales.txt.\n", nb_all);
    if (nb_all < NB_PLAY)
        printf("Attention : seulement %d questions disponibles (< %d).\n", nb_all, NB_PLAY);

    pick_random_questions();
    printf("Sélection aléatoire de %d questions pour cette partie.\n", nb_game);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(server_fd, 2) < 0) { perror("listen"); return 1; }

    printf("Serveur en attente de 2 joueurs sur le port %d...\n", PORT);

    int clients[2];
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    for (int i = 0; i < 2; i++) {
        clients[i] = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        if (clients[i] < 0) { perror("accept"); return 1; }
        printf("Joueur %d connecté.\n", i + 1);
        char id = '0' + i;
        send(clients[i], &id, 1, 0);
    }
    printf("La partie commence !\n");

    int score[2] = {0, 0};
    int tries[2] = {0, 0};
    int answered[2] = {0, 0};

    for (int q = 0; q < nb_game; q++) {
        memset(tries, 0, sizeof(tries));
        memset(answered, 0, sizeof(answered));

        const char *question = game_questions[q].question;
        const char *answer   = game_questions[q].answer;
        char last_msg[256] = "";
        int question_over = 0;

        struct timeval start, now;
        gettimeofday(&start, NULL);

        int last_sent_timer = -1;

        while (!question_over) {
            gettimeofday(&now, NULL);
            int elapsed = (int)(now.tv_sec - start.tv_sec);
            int remaining = TIME_LIMIT - elapsed;
            if (remaining < 0) remaining = 0;

            if (remaining != last_sent_timer) {
                for (int i = 0; i < 2; i++) {
                    send_state(clients[i], 'Q', remaining,
                               score[0], score[1], tries[0], tries[1],
                               last_msg, question);
                }
                last_sent_timer = remaining;
            }

            if (remaining == 0) {
                snprintf(last_msg, sizeof(last_msg), "Temps écoulé ! Réponse : %s", answer);
                for (int i = 0; i < 2; i++) {
                    send_state(clients[i], 'Q', 0,
                               score[0], score[1], tries[0], tries[1],
                               last_msg, question);
                }
                sleep(2);
                question_over = 1;
                break;
            }

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(clients[0], &rfds);
            FD_SET(clients[1], &rfds);
            struct timeval tv = {0, 200000};
            int maxfd = (clients[0] > clients[1] ? clients[0] : clients[1]) + 1;
            int sel = select(maxfd, &rfds, NULL, NULL, &tv);

            if (sel > 0) {
                for (int i = 0; i < 2; i++) {
                    if (!FD_ISSET(clients[i], &rfds)) continue;
                    if (answered[i]) {
                        char tmp[512];
                        read_line(clients[i], tmp, sizeof(tmp));
                        continue;
                    }
                    char line[512];
                    int r = read_line(clients[i], line, sizeof(line));
                    if (r <= 0) {
                        snprintf(last_msg, sizeof(last_msg), "Joueur %d déconnecté.", i + 1);
                        send_state(clients[1 - i], 'E', remaining,
                                   score[0], score[1], tries[0], tries[1],
                                   last_msg, question);
                        close(clients[0]);
                        close(clients[1]);
                        close(server_fd);
                        return 0;
                    }
                    tries[i]++;

                    if (answer_matches(line, answer)) {
                        score[i]++;
                        snprintf(last_msg, sizeof(last_msg),
                                 "Joueur %d a trouvé ! Réponse : %s", i + 1, answer);
                        for (int j = 0; j < 2; j++) {
                            send_state(clients[j], 'Q', remaining,
                                       score[0], score[1], tries[0], tries[1],
                                       last_msg, question);
                        }
                        sleep(2);
                        question_over = 1;
                        break;
                    } else {
                        if (tries[i] >= MAX_TRY) {
                            answered[i] = 1;
                            snprintf(last_msg, sizeof(last_msg),
                                     "Joueur %d : mauvaise réponse (plus de chances).", i + 1);
                        } else {
                            snprintf(last_msg, sizeof(last_msg),
                                     "Joueur %d : mauvaise réponse, il reste %d chance(s).",
                                     i + 1, MAX_TRY - tries[i]);
                        }
                        for (int j = 0; j < 2; j++) {
                            send_state(clients[j], 'Q', remaining,
                                       score[0], score[1], tries[0], tries[1],
                                       last_msg, question);
                        }
                    }
                }
            }
            if (question_over) break;

            if (answered[0] && answered[1]) {
                snprintf(last_msg, sizeof(last_msg), "Personne n'a trouvé. Réponse : %s", answer);
                for (int i = 0; i < 2; i++) {
                    send_state(clients[i], 'Q', remaining,
                               score[0], score[1], tries[0], tries[1],
                               last_msg, question);
                }
                sleep(2);
                question_over = 1;
            }
        }
    }

    char final_msg[256];
    if (score[0] > score[1])
        snprintf(final_msg, sizeof(final_msg), "Partie terminée. Joueur 1 gagne %d à %d !", score[0], score[1]);
    else if (score[1] > score[0])
        snprintf(final_msg, sizeof(final_msg), "Partie terminée. Joueur 2 gagne %d à %d !", score[0], score[1]);
    else
        snprintf(final_msg, sizeof(final_msg), "Partie terminée. Match nul %d à %d !", score[0], score[1]);

    for (int i = 0; i < 2; i++) {
        send_state(clients[i], 'E', 0, score[0], score[1], tries[0], tries[1],
                   final_msg, "");
    }

    close(clients[0]);
    close(clients[1]);
    close(server_fd);
    return 0;
}
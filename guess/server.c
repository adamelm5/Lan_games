#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/select.h>
#include <sys/time.h>

#define PORT 8080
#define MAX_ALL 2000
#define NB_PLAY 20
#define TIME_LIMIT 40
#define MAX_TRY 2

typedef struct { char question[512]; char answer[256]; } QA;
QA all_questions[MAX_ALL];
int nb_all = 0;
QA game_questions[NB_PLAY];
int nb_game = 0;

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
            have_q = 1;
        } else if (have_q && nb_all < MAX_ALL) {
            strncpy(all_questions[nb_all].question, pending_q, sizeof(all_questions[nb_all].question) - 1);
            strncpy(all_questions[nb_all].answer, line, sizeof(all_questions[nb_all].answer) - 1);
            nb_all++; have_q = 0;
        }
    }
    fclose(f);
    return nb_all;
}

void pick_random_questions() {
    srand((unsigned)time(NULL));
    int *idx = malloc(nb_all * sizeof(int));
    for (int i = 0; i < nb_all; i++) idx[i] = i;
    for (int i = nb_all - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
    }
    nb_game = (nb_all < NB_PLAY) ? nb_all : NB_PLAY;
    for (int i = 0; i < nb_game; i++) game_questions[i] = all_questions[idx[i]];
    free(idx);
}

// Normalisation : enlève accents, ponctuation, et met en minuscule
void normalize(const char *in, char *out, int maxlen) {
    int j = 0;
    for (int i = 0; in[i] && j < maxlen - 1; i++) {
        unsigned char c = in[i];
        if (c >= 'A' && c <= 'Z') out[j++] = c + 32;
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ') {
            if (c == ' ' && (j == 0 || out[j-1] == ' ')) continue; // pas d'espaces doubles
            out[j++] = c;
        }
        // Les accents courants (UTF-8 sur 2 octets)
        else if (c == 0xC3 && in[i+1]) {
            unsigned char n = in[i+1];
            if (n >= 0xA0 && n <= 0xA5) out[j++] = 'a';
            else if (n == 0xA7) out[j++] = 'c';
            else if (n >= 0xA8 && n <= 0xAB) out[j++] = 'e';
            else if (n >= 0xAC && n <= 0xAF) out[j++] = 'i';
            else if (n >= 0xB2 && n <= 0xB6) out[j++] = 'o';
            else if (n >= 0xB9 && n <= 0xBC) out[j++] = 'u';
            i++; // saute l'octet suivant
        }
    }
    if (j > 0 && out[j-1] == ' ') j--;
    out[j] = 0;
}

// Distance de Levenshtein
int levenshtein(const char *s1, const char *s2) {
    int len1 = strlen(s1), len2 = strlen(s2);
    int *d = malloc((len1 + 1) * (len2 + 1) * sizeof(int));
    #define D(i,j) d[(i)*(len2+1)+(j)]
    for (int i = 0; i <= len1; i++) D(i,0) = i;
    for (int j = 0; j <= len2; j++) D(0,j) = j;
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            int min = D(i-1,j) + 1;
            if (D(i,j-1) + 1 < min) min = D(i,j-1) + 1;
            if (D(i-1,j-1) + cost < min) min = D(i-1,j-1) + cost;
            D(i,j) = min;
        }
    }
    int res = D(len1, len2);
    free(d);
    return res;
    #undef D
}

int answer_matches(const char *given, const char *expected) {
    char g[256], e[256];
    normalize(given, g, sizeof(g));
    normalize(expected, e, sizeof(e));
    if (strcmp(g, e) == 0) return 1;
    // Tolérance : 1 typo max si le mot fait plus de 3 lettres
    if (strlen(e) > 3 && levenshtein(g, e) <= 1) return 1;
    return 0;
}

void send_state(int sock, char state, int timer, int scoreA, int scoreB, int tryA, int tryB, const char *msg, const char *question) {
    char buf[2048];
    snprintf(buf, sizeof(buf), "S %c %d %d %d %d %d %s|%s\n", state, timer, scoreA, scoreB, tryA, tryB, msg ? msg : "", question ? question : "");
    send(sock, buf, strlen(buf), 0);
}

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
    char ip[64] = "127.0.0.1";
    FILE *fip = fopen("ip.txt", "r");
    if (fip) { if (fgets(ip, sizeof(ip), fip)) ip[strcspn(ip, "\n")] = 0; fclose(fip); }
    printf("Serveur accessible sur l'IP : %s (port %d)\n", ip, PORT);

    if (load_questions("capitales.txt") < 0) return 1;
    if (nb_all == 0) { fprintf(stderr, "Aucune question chargee.\n"); return 1; }
    printf("Charge %d questions.\n", nb_all);
    pick_random_questions();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(server_fd, 2) < 0) { perror("listen"); return 1; }

    printf("En attente de 2 joueurs...\n");
    int clients[2];
    struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
    for (int i = 0; i < 2; i++) {
        clients[i] = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        printf("Joueur %d connecte.\n", i + 1);
        char id = '0' + i;
        send(clients[i], &id, 1, 0);
    }

    int score[2] = {0}, tries[2] = {0}, answered[2] = {0};
    for (int q = 0; q < nb_game; q++) {
        memset(tries, 0, sizeof(tries)); memset(answered, 0, sizeof(answered));
        const char *question = game_questions[q].question;
        const char *answer = game_questions[q].answer;
        char last_msg[256] = "";
        int question_over = 0;
        struct timeval start, now; gettimeofday(&start, NULL);
        int last_sent_timer = -1;

        while (!question_over) {
            gettimeofday(&now, NULL);
            int elapsed = (int)(now.tv_sec - start.tv_sec);
            int remaining = TIME_LIMIT - elapsed;
            if (remaining < 0) remaining = 0;
            if (remaining != last_sent_timer) {
                for (int i = 0; i < 2; i++) send_state(clients[i], 'Q', remaining, score[0], score[1], tries[0], tries[1], last_msg, question);
                last_sent_timer = remaining;
            }
            if (remaining == 0) {
                snprintf(last_msg, sizeof(last_msg), "Temps ecoule ! Reponse : %s", answer);
                for (int i = 0; i < 2; i++) send_state(clients[i], 'Q', 0, score[0], score[1], tries[0], tries[1], last_msg, question);
                sleep(2); question_over = 1; break;
            }
            fd_set rfds; FD_ZERO(&rfds); FD_SET(clients[0], &rfds); FD_SET(clients[1], &rfds);
            struct timeval tv = {0, 200000};
            int maxfd = (clients[0] > clients[1] ? clients[0] : clients[1]) + 1;
            int sel = select(maxfd, &rfds, NULL, NULL, &tv);
            if (sel > 0) {
                for (int i = 0; i < 2; i++) {
                    if (!FD_ISSET(clients[i], &rfds)) continue;
                    if (answered[i]) { char tmp[512]; read_line(clients[i], tmp, sizeof(tmp)); continue; }
                    char line[512]; int r = read_line(clients[i], line, sizeof(line));
                    if (r <= 0) { goto game_end; }
                    tries[i]++;
                    if (answer_matches(line, answer)) {
                        score[i]++;
                        snprintf(last_msg, sizeof(last_msg), "Joueur %d a trouve ! Reponse : %s", i + 1, answer);
                        for (int j = 0; j < 2; j++) send_state(clients[j], 'Q', remaining, score[0], score[1], tries[0], tries[1], last_msg, question);
                        sleep(2); question_over = 1; break;
                    } else {
                        if (tries[i] >= MAX_TRY) { answered[i] = 1; snprintf(last_msg, sizeof(last_msg), "Joueur %d : plus de chances.", i + 1); }
                        else snprintf(last_msg, sizeof(last_msg), "Joueur %d : mauvaise reponse (%d chance(s)).", i + 1, MAX_TRY - tries[i]);
                        for (int j = 0; j < 2; j++) send_state(clients[j], 'Q', remaining, score[0], score[1], tries[0], tries[1], last_msg, question);
                    }
                }
            }
            if (question_over) break;
            if (answered[0] && answered[1]) {
                snprintf(last_msg, sizeof(last_msg), "Personne n'a trouve. Reponse : %s", answer);
                for (int i = 0; i < 2; i++) send_state(clients[i], 'Q', remaining, score[0], score[1], tries[0], tries[1], last_msg, question);
                sleep(2); question_over = 1;
            }
        }
    }
    char final_msg[256];
    if (score[0] > score[1]) snprintf(final_msg, sizeof(final_msg), "Joueur 1 gagne %d a %d !", score[0], score[1]);
    else if (score[1] > score[0]) snprintf(final_msg, sizeof(final_msg), "Joueur 2 gagne %d a %d !", score[1], score[0]);
    else snprintf(final_msg, sizeof(final_msg), "Match nul %d a %d !", score[0], score[1]);
    for (int i = 0; i < 2; i++) send_state(clients[i], 'E', 0, score[0], score[1], tries[0], tries[1], final_msg, "");
    
game_end:
    close(clients[0]); close(clients[1]); close(server_fd);
    return 0;
}

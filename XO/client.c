#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define SIZE 9

#define RED     "\033[1;31m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define RESET   "\033[0m"

void clear_screen() { printf("\033[2J\033[H"); }

// Affiche une case :
//  - vide  -> numéro (1-9) en jaune
//  - 'X'   -> X en rouge
//  - 'O'   -> O en vert
void print_cell(char c, int index) {
    if (c == ' ') {
        printf(" " YELLOW "%d" RESET " ", index + 1);
    } else if (c == 'X') {
        printf(" " RED "X" RESET " ");
    } else if (c == 'O') {
        printf(" " GREEN "O" RESET " ");
    } else {
        printf(" %c ", c);
    }
}

void print_board(char board[SIZE], char my_symbol) {
    clear_screen();
    printf("=== MORPION RESEAU ===\n\n");
    for (int i = 0; i < 3; i++) {
        print_cell(board[i*3],     i*3);
        printf("|");
        print_cell(board[i*3 + 1], i*3 + 1);
        printf("|");
        print_cell(board[i*3 + 2], i*3 + 2);
        printf("\n");
        if (i < 2) printf("---+---+---\n");
    }
    printf("\n");
    // Indication du symbole du joueur, coloré
    if (my_symbol == 'X') {
        printf("Vous êtes : " RED "X" RESET "\n\n");
    } else {
        printf("Vous êtes : " GREEN "O" RESET "\n\n");
    }
}

int main(void) {
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) { perror("socket"); return -1; }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton"); return -1;
    }

    printf("Connexion au serveur...\n");
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect"); return -1;
    }

    printf("En attente d'un autre joueur...\n");

    // 1) Recevoir le symbole (1 octet)
    char my_symbol = '?';
    if (recv(sock, &my_symbol, 1, 0) <= 0) {
        printf("Connexion perdue.\n");
        return 0;
    }

    char board[SIZE];
    char state;

    while (1) {
        if (recv(sock, &state, 1, 0) <= 0) break;
        if (recv(sock, board, SIZE, 0) <= 0) break;

        print_board(board, my_symbol);

        if (state == 'E') {
            printf("Partie terminée.\n");
            break;
        }

        if (state == 'T') {
            char input[16];
            printf("Entrez une position (1-9) : ");
            fflush(stdout);
            if (fgets(input, sizeof(input), stdin) == NULL) break;

            char c = input[0];
            if (c >= '1' && c <= '9') {
                send(sock, &c, 1, 0);
            } else {
                char bad = '0';
                send(sock, &bad, 1, 0);
            }
        } else {
            printf("En attente de l'autre joueur...\n");
            printf("(le plateau se mettra à jour automatiquement)\n");
        }
    }

    close(sock);
    return 0;
}
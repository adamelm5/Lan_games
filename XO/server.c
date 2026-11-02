#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define SIZE 9

int check_win(char board[SIZE]) {
    int wins[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    for (int i = 0; i < 8; i++) {
        if (board[wins[i][0]] != ' ' &&
            board[wins[i][0]] == board[wins[i][1]] &&
            board[wins[i][1]] == board[wins[i][2]])
            return 1;
    }
    return 0;
}

void send_state(int sock, char state, char board[SIZE]) {
    send(sock, &state, 1, 0);
    send(sock, board, SIZE, 0);
}

int main() {
    int server_fd, client1, client2;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char board[SIZE];
    memset(board, ' ', SIZE);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { perror("bind"); exit(1); }
    if (listen(server_fd, 2) < 0) { perror("listen"); exit(1); }

    printf("Serveur en attente de 2 joueurs sur le port %d...\n", PORT);

    client1 = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    printf("Joueur 1 connecté.\n");
    client2 = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    printf("Joueur 2 connecté. La partie commence !\n");

    // Envoi du symbole à chaque joueur (1 octet)
    char sym1 = 'X', sym2 = 'O';
    send(client1, &sym1, 1, 0);
    send(client2, &sym2, 1, 0);

    int current = 0;
    int moves = 0;
    int game_over = 0;

    send_state(client1, 'T', board);
    send_state(client2, 'W', board);

    while (!game_over && moves < 9) {
        int active   = (current == 0) ? client1 : client2;
        int inactive = (current == 0) ? client2 : client1;
        char move;

        int n = recv(active, &move, 1, 0);
        if (n <= 0) break;

        int pos = move - '1';
        if (pos < 0 || pos >= SIZE || board[pos] != ' ') {
            send_state(active, 'T', board);
            continue;
        }

        board[pos] = (current == 0) ? 'X' : 'O';
        moves++;

        if (check_win(board)) {
            game_over = 1;
            send_state(active,   'E', board);
            send_state(inactive, 'E', board);
        } else if (moves == 9) {
            game_over = 1;
            send_state(active,   'E', board);
            send_state(inactive, 'E', board);
        } else {
            current = 1 - current;
            send_state(client1, (current == 0) ? 'T' : 'W', board);
            send_state(client2, (current == 1) ? 'T' : 'W', board);
        }
    }

    printf("Partie terminée.\n");
    close(client1);
    close(client2);
    close(server_fd);
    return 0;
}
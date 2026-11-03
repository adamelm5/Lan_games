#!/bin/bash

# 1. Récupérer l'IP locale (celle de l'interface réseau active)
IP=$(ip route get 1.1.1.1 | grep -oP 'src \K\S+')

if [ -z "$IP" ]; then
    echo "Erreur : Impossible de détecter l'IP locale."
    exit 1
fi

echo "L'IP locale détectée est : $IP"

# 2. L'écrire dans un fichier de config pour que le serveur et le client le lisent
echo "$IP" > ip.txt

# 3. Lancer le serveur
echo "Lancement du serveur sur le port 8080..."
./server

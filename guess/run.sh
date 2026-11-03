#!/bin/bash
IP=$(ip route get 1.1.1.1 | grep -oP 'src \K\S+')
if [ -z "$IP" ]; then
    echo "Erreur : Impossible de détecter l'IP locale."
    exit 1
fi
echo "IP locale détectée : $IP"
echo "$IP" > ip.txt
echo "Lancement du serveur..."
./server

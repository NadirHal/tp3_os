#ifndef BEUIP_H
#define BEUIP_H

#include <arpa/inet.h>

#define LPSEUDO 23
#define BROADCAST_IP "192.168.88.255"

// Structure du message
typedef struct {
    char code;
    char verif_beuip[5];
    char message[256];
} formatMessage;

// Prototypes
void beuip_start(char *pseudo);
void beuip_stop();
void commande(char octet1, char *message, char *pseudo);
void listeElts(void);

// Prototypes 
void demandeListe(char *pseudo);
void demandeFichier(char *pseudo, char *nomfic);

#endif
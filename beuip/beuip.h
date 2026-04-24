#ifndef BEUIP_H
#define BEUIP_H

#include <arpa/inet.h>

#define LPSEUDO 23

// format du message UDP
typedef struct {
    char code;
    char verif_beuip[5];
    char message[256];
} formatMessage;

// prototypes
void beuip_start(char *pseudo);
void beuip_stop();
void commande(char octet1, char *message, char *pseudo);
void listeElts(void);

// nouvelles fonctions pour l'etape 3
void demandeListe(char *pseudo);
void demandeFichier(char *pseudo, char *nomfic);

#endif
#include "beuip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>

#define PORT 9998

// le chainage des couples (pseudo, IPV4)
struct elt {
    char nom[LPSEUDO + 1];
    char adip[16];
    struct elt *next;
};

struct elt *annuaire = NULL; 
pthread_mutex_t verrou_liste = PTHREAD_MUTEX_INITIALIZER;

pthread_t thread_serveur;
pthread_t thread_tcp; 
int serveur_actif = 0; 
int sid;
struct sockaddr_in BroadcastSock;

char repertoire_pub[256] = "reppub";

void ajouteElt(char *pseudo, char *adip) {
    pthread_mutex_lock(&verrou_liste);
    struct elt *courant = annuaire;
    while (courant != NULL) {
        if (strcmp(courant->adip, adip) == 0) {
            pthread_mutex_unlock(&verrou_liste);
            return; 
        }
        courant = courant->next;
    }
    struct elt *nouveau = malloc(sizeof(struct elt));
    strncpy(nouveau->nom, pseudo, LPSEUDO);
    nouveau->nom[LPSEUDO] = '\0';
    strncpy(nouveau->adip, adip, 15);
    nouveau->adip[15] = '\0';
    
    struct elt **prec = &annuaire;
    while (*prec != NULL && strcmp((*prec)->nom, pseudo) < 0) {
        prec = &((*prec)->next);
    }
    nouveau->next = *prec;
    *prec = nouveau;
    pthread_mutex_unlock(&verrou_liste);
}

void listeElts(void) {
    pthread_mutex_lock(&verrou_liste);
    struct elt *courant = annuaire;
    while (courant != NULL) {
        // Affichage strict 
        printf("%s : %s\n", courant->adip, courant->nom);
        courant = courant->next;
    }
    pthread_mutex_unlock(&verrou_liste);
}

struct in_addr recupererAdresse(char *pseudo) {
    struct in_addr addr;
    addr.s_addr = 0; 
    pthread_mutex_lock(&verrou_liste);
    struct elt *courant = annuaire;
    while (courant != NULL) {
        if (strcmp(courant->nom, pseudo) == 0) {
            inet_aton(courant->adip, &addr);
            break;
        }
        courant = courant->next;
    }
    pthread_mutex_unlock(&verrou_liste);
    return addr;
}

void supprimerElt(char* adresse) {
    pthread_mutex_lock(&verrou_liste);
    struct elt *courant = annuaire, *precedent = NULL;
    while (courant != NULL) {
        if (strcmp(courant->adip, adresse) == 0) {
            if (precedent == NULL) annuaire = courant->next;
            else precedent->next = courant->next;
            free(courant);
            pthread_mutex_unlock(&verrou_liste);
            return;
        }
        precedent = courant;
        courant = courant->next;
    }
    pthread_mutex_unlock(&verrou_liste);
}

// Fonction isolée pour respecter la taille < 20 lignes
void traiterMessageUDP(formatMessage *msg, struct sockaddr_in *sender, char *mon_pseudo) {
    if(strncmp(msg->verif_beuip, "BEUIP", 5) != 0) return;
    char *ip = inet_ntoa(sender->sin_addr);

    if(msg->code == '1') {
        ajouteElt(msg->message, ip);
        formatMessage rep;
        rep.code = '2'; memcpy(rep.verif_beuip, "BEUIP", 5);
        strncpy(rep.message, mon_pseudo, 256);
        sendto(sid, &rep, sizeof(rep), 0, (struct sockaddr *)sender, sizeof(*sender));
    }
    else if (msg->code == '2') ajouteElt(msg->message, ip);
    else if (msg->code == '9') printf("\n[%s] : %s\n", ip, msg->message);
    else if (msg->code == '0') supprimerElt(ip);
}

void* serveur_udp(void* p) {
    char* pseudo = (char* ) p;
    struct sockaddr_in Sock, SockConf;
    socklen_t ls = sizeof(Sock);
    formatMessage msg_envoi, msg_recu;

    if ((sid=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0) return NULL;
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = INADDR_ANY;

    int activeBroadcast = 1;
    setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &activeBroadcast, sizeof(activeBroadcast));
    if (bind(sid, (struct sockaddr *) &SockConf, sizeof(SockConf)) == -1) return NULL;

    msg_envoi.code = '1'; memcpy(msg_envoi.verif_beuip, "BEUIP", 5);
    strncpy(msg_envoi.message, pseudo, 256);
    sendto(sid, &msg_envoi, sizeof(msg_envoi), 0, (struct sockaddr *)&BroadcastSock, sizeof(BroadcastSock));

    for (;;) {
        memset(&msg_recu, 0, sizeof(formatMessage));
        if (recvfrom(sid, &msg_recu, sizeof(formatMessage), 0,(struct sockaddr *)&Sock, &ls) != -1) {
            traiterMessageUDP(&msg_recu, &Sock, pseudo);
        }
    }
    return NULL;
}

// Partie 3 - Bonus TCP (Envoi de la commande et du fichier)
void envoiContenu(int fd) {
    char code;
    if (read(fd, &code, 1) <= 0) { close(fd); return; }
    if (code == 'L') {
        if (fork() == 0) {
            dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO);
            execlp("ls", "ls", "-l", repertoire_pub, NULL);
            exit(1);
        }
    } else if (code == 'F') {
        char nomfic[256]; int i = 0; char c;
        while (read(fd, &c, 1) > 0 && c != '\n' && i < 255) nomfic[i++] = c;
        nomfic[i] = '\0';
        char chemin[512]; snprintf(chemin, sizeof(chemin), "%s/%s", repertoire_pub, nomfic);
        if (fork() == 0) {
            dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO);
            execlp("cat", "cat", chemin, NULL);
            exit(1);
        }
    }
    close(fd);
}

void* serveur_tcp(void* p) {
    (void)p;
    int sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET; saddr.sin_addr.s_addr = INADDR_ANY; saddr.sin_port = htons(PORT);
    int opt = 1; setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(sock_tcp, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) return NULL;
    listen(sock_tcp, 5);
    
    while(1) {
        struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
        int cfd = accept(sock_tcp, (struct sockaddr*)&caddr, &clen);
        if (cfd >= 0) envoiContenu(cfd);
    }
    return NULL;
}

void commande (char octet1, char * message, char * pseudo) {
    formatMessage msg; 
    struct sockaddr_in dest;
    msg.code = '9'; 
    memcpy(msg.verif_beuip, "BEUIP", 5); 

    if(octet1 == '3') listeElts();
    else if(octet1 == '4') {
        strncpy(msg.message, message, 256);
        dest.sin_family = AF_INET; dest.sin_port = htons(PORT);
        dest.sin_addr = recupererAdresse(pseudo); 
        if (dest.sin_addr.s_addr != 0) sendto(sid, &msg, sizeof(msg), 0, (struct sockaddr *)&dest, sizeof(dest));
        else printf("Erreur : %s inconnu.\n", pseudo);
    }
    else if(octet1 == '5') {
        strncpy(msg.message, message, 256);
        sendto(sid, &msg, sizeof(msg), 0, (struct sockaddr *)&BroadcastSock, sizeof(BroadcastSock));
    }
}

void demandeListe(char *pseudo) {
    struct in_addr addr = recupererAdresse(pseudo);
    if(addr.s_addr == 0) return;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dest; dest.sin_family = AF_INET; dest.sin_port = htons(PORT); dest.sin_addr = addr;
    if(connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0) return;
    write(sock, "L", 1);
    char buf[512]; int n;
    while((n = read(sock, buf, sizeof(buf)-1)) > 0) { buf[n] = '\0'; printf("%s", buf); }
    close(sock);
}

void demandeFichier(char *pseudo, char *nomfic) {
    struct in_addr addr = recupererAdresse(pseudo);
    if(addr.s_addr == 0) return;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dest; dest.sin_family = AF_INET; dest.sin_port = htons(PORT); dest.sin_addr = addr;
    if(connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0) return;
    char requete[512]; snprintf(requete, sizeof(requete), "F%s\n", nomfic);
    write(sock, requete, strlen(requete));
    
    char chemin[512]; snprintf(chemin, sizeof(chemin), "%s/%s", repertoire_pub, nomfic);
    FILE *f = fopen(chemin, "w");
    if (f != NULL) {
        char buf[1024]; int n;
        while((n = read(sock, buf, sizeof(buf))) > 0) fwrite(buf, 1, n, f);
        fclose(f);
    }
    close(sock);
}

void beuip_start(char *pseudo) {
    if (serveur_actif) return;
    struct stat st = {0};
    if (stat(repertoire_pub, &st) == -1) mkdir(repertoire_pub, 0777);

    // Initialisation globale de la socket de broadcast 
    BroadcastSock.sin_family = AF_INET;
    BroadcastSock.sin_port = htons(PORT);
    inet_aton(BROADCAST_IP, &BroadcastSock.sin_addr);

    pthread_create(&thread_serveur, NULL, serveur_udp, (void *)pseudo);
    pthread_create(&thread_tcp, NULL, serveur_tcp, NULL);
    serveur_actif = 1;
}

void beuip_stop() {
    if (!serveur_actif) return;
    formatMessage msg_fin; msg_fin.code = '0'; memcpy(msg_fin.verif_beuip, "BEUIP", 5);
    
    pthread_mutex_lock(&verrou_liste);
    struct elt *courant = annuaire; 
    struct sockaddr_in dest;
    dest.sin_family = AF_INET; dest.sin_port = htons(PORT);
    
    while (courant != NULL) {
        inet_aton(courant->adip, &dest.sin_addr);
        sendto(sid, &msg_fin, sizeof(msg_fin), 0, (struct sockaddr *)&dest, sizeof(dest));
        struct elt *tmp = courant; 
        courant = courant->next; 
        free(tmp); // Libération de la mémoire pour Valgrind
    }
    annuaire = NULL; 
    pthread_mutex_unlock(&verrou_liste);

    pthread_cancel(thread_serveur); pthread_join(thread_serveur, NULL);
    pthread_cancel(thread_tcp); pthread_join(thread_tcp, NULL);
    close(sid); serveur_actif = 0;
}
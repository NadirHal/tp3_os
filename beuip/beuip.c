#include "beuip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <ifaddrs.h> // pour getifaddrs

#define PORT 9998

// maillon de l'annuaire
struct elt {
    char nom[LPSEUDO + 1];
    char adip[16];
    struct elt *next;
};

struct elt *annuaire = NULL; // tete de liste
pthread_mutex_t verrou_liste = PTHREAD_MUTEX_INITIALIZER;

pthread_t thread_serveur;
pthread_t thread_tcp; // pour le partage de fichiers
int serveur_actif = 0; // 0 = arreté, 1 = en cours

char repertoire_pub[256] = "reppub"; // nom du dossier partagé

void ajouteElt(char *pseudo, char *adip) {
    pthread_mutex_lock(&verrou_liste);

    // evite de dupliquer si le mec est deja la
    struct elt *courant = annuaire;
    while (courant != NULL) {
        if (strcmp(courant->adip, adip) == 0) {
            pthread_mutex_unlock(&verrou_liste);
            return; 
        }
        courant = courant->next;
    }

    // on alloue la memoire pour le nouveau
    struct elt *nouveau = malloc(sizeof(struct elt));
    strncpy(nouveau->nom, pseudo, LPSEUDO);
    nouveau->nom[LPSEUDO] = '\0';
    strncpy(nouveau->adip, adip, 15);
    nouveau->adip[15] = '\0';

    // tri alphabetique
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
    
    if (courant == NULL) {
        printf("Aucun utilisateur connecté.\n");
    } else {
        while (courant != NULL) {
            printf("- %s (%s)\n", courant->nom, courant->adip);
            courant = courant->next;
        }
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

    struct elt *courant = annuaire;
    struct elt *precedent = NULL;

    while (courant != NULL) {
        if (strcmp(courant->adip, adresse) == 0) {
            if (precedent == NULL) {
                annuaire = courant->next;
            } else {
                precedent->next = courant->next;
            }
            free(courant);
            printf("Contact %s supprimé.\n", adresse);
            pthread_mutex_unlock(&verrou_liste);
            return;
        }
        precedent = courant;
        courant = courant->next;
    }

    pthread_mutex_unlock(&verrou_liste);
}

int sid;
struct sockaddr_in Sock, SockConf, BroadcastSock;

void* serveur_udp(void* p) {
    char* pseudo = (char* ) p;
    int n;
    socklen_t ls = sizeof(Sock);
    formatMessage msg_envoi, msg_recu;

    if ((sid=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0) {
        perror("socket");
        return NULL;
    }

    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = INADDR_ANY;

    int activeBroadcast = 1;
    setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &activeBroadcast, sizeof(activeBroadcast));

    if (bind(sid, (struct sockaddr *) &SockConf, sizeof(SockConf)) == -1) {
        perror("bind");
        return NULL;
    }
    printf("Serveur UDP sur le port %d\n", PORT);

    // Etape 2.1 : getifaddrs pour trouver les broadcast
    struct ifaddrs *ifap, *ifa;
    msg_envoi.code = '1';
    memcpy(msg_envoi.verif_beuip, "BEUIP", 5);
    strncpy(msg_envoi.message, pseudo, 256);

    if (getifaddrs(&ifap) != -1) {
        for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr != NULL && ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_broadaddr;
                if (sa != NULL) {
                    char *ip_broad = inet_ntoa(sa->sin_addr);
                    // on ignore loopback
                    if (strcmp(ip_broad, "127.255.255.255") != 0 && strcmp(ip_broad, "0.0.0.0") != 0) {
                        BroadcastSock.sin_family = AF_INET;
                        BroadcastSock.sin_port = htons(PORT);
                        BroadcastSock.sin_addr = sa->sin_addr;
                        sendto(sid, &msg_envoi, sizeof(msg_envoi), 0, (struct sockaddr *)&BroadcastSock, sizeof(BroadcastSock));
                    }
                }
            }
        }
        freeifaddrs(ifap);
    }

    for (;;) {
        memset(&msg_recu, 0, sizeof(formatMessage));
        
        if ((n=recvfrom(sid, &msg_recu, sizeof(formatMessage), 0,(struct sockaddr *)&Sock, &ls)) == -1) {
            continue;
        }

        if(!strncmp(msg_recu.verif_beuip, "BEUIP", 5)) {
            if(msg_recu.code == '1') {
                ajouteElt(msg_recu.message, inet_ntoa(Sock.sin_addr));

                msg_envoi.code = '2';
                strncpy(msg_envoi.verif_beuip, msg_recu.verif_beuip, 5);
                strncpy(msg_envoi.message, pseudo, 256);
                sendto(sid, &msg_envoi, sizeof(msg_envoi), MSG_CONFIRM, (struct sockaddr *)&Sock, ls);
            }
            else if (msg_recu.code == '2') {
                ajouteElt(msg_recu.message, inet_ntoa(Sock.sin_addr));
            }
            else if (msg_recu.code == '9') {
                printf("\n[Message de %s] : %s\n", inet_ntoa(Sock.sin_addr), msg_recu.message);
            }
            else if (msg_recu.code == '0') {
                supprimerElt(inet_ntoa(Sock.sin_addr));
            }
        }
    }
    return NULL;
}

// Partie Serveur TCP (Etape 3)
void envoiContenu(int fd) {
    char code;
    if (read(fd, &code, 1) <= 0) {
        close(fd);
        return;
    }

    if (code == 'L') {
        if (fork() == 0) {
            // on redirige la sortie du ls vers la socket client
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            execlp("ls", "ls", "-l", repertoire_pub, NULL);
            exit(1);
        }
    } else if (code == 'F') {
        char nomfic[256];
        int i = 0;
        char c;
        // on lit le nom du fichier jusqu'au retour a la ligne
        while (read(fd, &c, 1) > 0 && c != '\n' && i < 255) {
            nomfic[i++] = c;
        }
        nomfic[i] = '\0';

        char chemin[512];
        snprintf(chemin, sizeof(chemin), "%s/%s", repertoire_pub, nomfic);

        if (fork() == 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            execlp("cat", "cat", chemin, NULL);
            exit(1);
        }
    }
    close(fd);
}

void* serveur_tcp(void* p) {
    int sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(PORT); // meme port 9998
    
    int opt = 1;
    setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sock_tcp, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        perror("bind tcp");
        return NULL;
    }
    listen(sock_tcp, 5);
    
    printf("Serveur TCP prêt pour les fichiers\n");

    while(1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(sock_tcp, (struct sockaddr*)&caddr, &clen);
        if (cfd >= 0) {
            envoiContenu(cfd);
        }
    }
    return NULL;
}


void commande (char octet1, char * message, char * pseudo) {
    formatMessage msg_envoi;
    struct sockaddr_in dest_addr;

    switch(octet1) {
        case '3': 
            printf("--- Liste des contacts ---\n");
            listeElts();
            break;

        case '4': 
            msg_envoi.code = '9';
            memcpy(msg_envoi.verif_beuip, "BEUIP", 5);
            strncpy(msg_envoi.message, message, 256);
            
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(PORT);
            dest_addr.sin_addr = recupererAdresse(pseudo); 

            if (dest_addr.sin_addr.s_addr != 0) {
                sendto(sid, &msg_envoi, sizeof(msg_envoi), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            } else {
                printf("Erreur : Pseudo introuvable.\n");
            }
            break;

        case '5': 
            msg_envoi.code = '9';
            memcpy(msg_envoi.verif_beuip, "BEUIP", 5);
            strncpy(msg_envoi.message, message, 256);
            sendto(sid, &msg_envoi, sizeof(msg_envoi), 0, (struct sockaddr *)&BroadcastSock, sizeof(BroadcastSock));
            break;
    }
}

// Fonction Client TCP (demander liste)
void demandeListe(char *pseudo) {
    struct in_addr addr = recupererAdresse(pseudo);
    if(addr.s_addr == 0) {
        printf("Contact inconnu.\n");
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PORT);
    dest.sin_addr = addr;

    if(connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        perror("connect");
        return;
    }

    write(sock, "L", 1);
    char buf[512];
    int n;
    printf("\nFichiers de %s :\n", pseudo);
    while((n = read(sock, buf, sizeof(buf)-1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    close(sock);
}

// Fonction Client TCP (telecharger fichier)
void demandeFichier(char *pseudo, char *nomfic) {
    struct in_addr addr = recupererAdresse(pseudo);
    if(addr.s_addr == 0) {
        printf("Contact inconnu.\n");
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PORT);
    dest.sin_addr = addr;

    if(connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        perror("connect");
        return;
    }

    char requete[512];
    snprintf(requete, sizeof(requete), "F%s\n", nomfic);
    write(sock, requete, strlen(requete));

    char chemin_local[512];
    snprintf(chemin_local, sizeof(chemin_local), "%s/%s", repertoire_pub, nomfic);
    
    FILE *f = fopen(chemin_local, "w");
    if (f != NULL) {
        char buf[1024];
        int n;
        while((n = read(sock, buf, sizeof(buf))) > 0) {
            fwrite(buf, 1, n, f);
        }
        fclose(f);
        printf("Fichier telecharge dans %s !\n", chemin_local);
    } else {
        perror("fopen");
    }
    close(sock);
}


void beuip_start(char *pseudo) {
    if (serveur_actif) {
        printf("Deja lancé.\n");
        return;
    }

    // on verifie que le dossier pub existe, sinon on le cree
    struct stat st = {0};
    if (stat(repertoire_pub, &st) == -1) {
        mkdir(repertoire_pub, 0777);
    }

    pthread_create(&thread_serveur, NULL, serveur_udp, (void *)pseudo);
    pthread_create(&thread_tcp, NULL, serveur_tcp, NULL); // lance tcp aussi
    
    serveur_actif = 1;
    printf("BEUIP démarré (%s)\n", pseudo);
}

void beuip_stop() {
    if (!serveur_actif) return;

    formatMessage msg_fin;
    msg_fin.code = '0';
    memcpy(msg_fin.verif_beuip, "BEUIP", 5);

    pthread_mutex_lock(&verrou_liste);
    struct elt *courant = annuaire;
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PORT);

    while (courant != NULL) {
        inet_aton(courant->adip, &dest.sin_addr);
        sendto(sid, &msg_fin, sizeof(msg_fin), 0, (struct sockaddr *)&dest, sizeof(dest));
        
        struct elt *a_supprimer = courant;
        courant = courant->next;
        free(a_supprimer);
    }
    annuaire = NULL;
    pthread_mutex_unlock(&verrou_liste);

    // on arrete violemment les threads (plus simple)
    pthread_cancel(thread_serveur);
    pthread_cancel(thread_tcp);
    
    close(sid);
    serveur_actif = 0;
    printf("Serveurs arrêtés.\n");
}
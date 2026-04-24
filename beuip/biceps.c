#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>

#include "beuip.h"

static char **Mots = NULL;
static int NMots = 0;
int analyseCom(char *b);
char *copyString(char *s);

#define NBMAXC 10 
typedef int (*cmd_ptr)(int, char **);
typedef struct { char *nom; cmd_ptr fct; } Command;
static Command tabComInt[NBMAXC];
static int nbComActual = 0;

void ajouteCom(char *nom, cmd_ptr fct);
void majComInt(void);
void listeComInt(void);
int execComInt(int N, char **P);
int execComExt(char **P);

int Sortie(int N, char *P[]);
int Cd(int N, char *P[]);
int Pwd(int N, char *P[]);
int Vers(int N, char *P[]);
int Beuip(int N, char *P[]);

int main() {
    int run = 1;
    char *user = getenv("USER");
    char host[64]; gethostname(host, 64);
    char *prompt;
    
    if(geteuid() == 0) asprintf(&prompt, "%s@%s# ", user, host);
    else asprintf(&prompt, "%s@%s$ ", user, host);

    majComInt();

    char *input, *commande_seule, *ptr_input; 

    while(run) {
        input = readline(prompt);
        if (!input) break; // Gestion du CTRL+D
        add_history(input);

        ptr_input = input; 
        while ((commande_seule = strsep(&ptr_input, ";")) != NULL) {
            if (commande_seule[0] == '\0') continue;
            char *copie = copyString(commande_seule);
            analyseCom(copie);

            if (NMots > 0) {
                if (!execComInt(NMots, Mots)) {
                    if (!execComExt(Mots)) {
                        fprintf(stderr, "MonShell: %s: commande introuvable\n", Mots[0]);
                    }
                }
            }
            free(copie); 
        }
        free(input); 
    }

    // Libération de la mémoire pour Valgrind à la fermeture
    beuip_stop();
    free(Mots);
    clear_history();
    free(prompt);
    
    return 0;
}

int analyseCom(char *b) {
    free(Mots);
    NMots = 0;
    char *copie_comptage = copyString(b);
    char *mot;
    while((mot = strsep(&copie_comptage, " ")) != NULL) {
        if (mot[0] != '\0') NMots++;
    }
    free(copie_comptage);

    Mots = malloc(sizeof(char *) * (NMots + 1));
    int i = 0;
    while((mot = strsep(&b, " ")) != NULL) {
        if (mot[0] != '\0') { Mots[i] = mot; i++; }
    }
    Mots[i] = NULL;
    return 0;
}

char *copyString(char *s) {
    int len = strlen(s);
    char *copie = malloc(sizeof(char) * (len + 1));
    strcpy(copie, s);
    return copie;
}

void ajouteCom(char *nom, cmd_ptr fct) {
    if (nbComActual < NBMAXC) {
        tabComInt[nbComActual].nom = nom;
        tabComInt[nbComActual].fct = fct;
        nbComActual++;
    }
}

void majComInt(void) {
    ajouteCom("exit", Sortie);
    ajouteCom("cd", Cd);
    ajouteCom("pwd", Pwd);
    ajouteCom("vers", Vers);
    ajouteCom("beuip", Beuip);
}

void listeComInt(void) {
    printf("%d commandes internes\n", nbComActual);
    for(int i = 0; i < nbComActual; i++) printf("> %s\n", tabComInt[i].nom);
}

int execComInt(int N, char **P) {
    for(int i = 0; i < nbComActual; i ++) {
        if(!strcmp(P[0], tabComInt[i].nom)) {
            tabComInt[i].fct(N, P);
            return 1;
        }
    }
    return 0;
}

int execComExt(char **P) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(P[0], P);
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        int status; waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) return 0;
        return 1;
    }
    return 0;
}

int Sortie(int N, char *P[]) {
    (void)N; (void)P;
    beuip_stop();
    free(Mots);
    clear_history();
    exit(0);
}

int Cd(int N, char *P[]) {
    if (N > 1) chdir(P[1]);
    return 1;
}

int Pwd(int N, char *P[]) {
    (void)N; (void)P;
    char cwd[1024]; 
    if (getcwd(cwd, sizeof(cwd)) != NULL) printf("%s\n", cwd);
    return 1;
}

int Vers(int N, char *P[]) {
    (void)N; (void)P;
    printf("version 3.0\n");
    return 1;
}

int Beuip(int N, char *P[]) {
    if (N < 2) return 0;

    if (strcmp(P[1], "start") == 0 && N > 2) beuip_start(P[2]);
    else if (strcmp(P[1], "stop") == 0) beuip_stop();
    else if (strcmp(P[1], "list") == 0) commande('3', NULL, NULL);
    else if (strcmp(P[1], "message") == 0 && N >= 4) {
        // Reconstitution du message complet s'il contient des espaces 
        char msg_complet[256] = "";
        for (int i = 3; i < N; i++) {
            strcat(msg_complet, P[i]);
            if (i < N - 1) strcat(msg_complet, " ");
        }
        
        if (strcmp(P[2], "all") == 0) commande('5', msg_complet, NULL);
        else commande('4', msg_complet, P[2]);
    }
    // Commandes bonus 
    else if (strcmp(P[1], "ls") == 0 && N > 2) demandeListe(P[2]);
    else if (strcmp(P[1], "get") == 0 && N > 3) demandeFichier(P[2], P[3]);
    
    return 1;
}
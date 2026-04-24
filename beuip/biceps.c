#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/wait.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>

#include "beuip.h"

// Ce tableau contiendra les adresses des mots de la commande, pas leur copie,
// c'est pour quoi il faut faire attention a ne pas endommager le char * de la commande
// d'ou les differentes copies
static char **Mots = NULL;
static int NMots = 0;
int analyseCom(char *b);

char *copyString(char *s);

// TABLEAUX COMMANDES INTERNES
#define NBMAXC 10 /* Nb maxi de commandes internes */

typedef int (*cmd_ptr)(int, char **);

typedef struct {
    char *nom;
    cmd_ptr fct;
} Command;

static Command tabComInt[NBMAXC];

static int nbComActual = 0;

void ajouteCom(char *nom, cmd_ptr fct);

void majComInt(void);

void listeComInt(void);

int execComInt(int N, char **P);

int execComExt(char **P);

// COMMANDES INTERNES

int Sortie(int N, char *P[]);

int Cd(int N, char *P[]);

int Pwd(int N, char *P[]);

int Vers(int N, char *P[]);

// MAIN

int main()
{
    int run = 1;

    // Construire l'affichage dans le terminal (utilisateur actuel...)
    char *user;
    char host[64];
    char *prompt;

    user = getenv("USER");
    gethostname(host, 64);

    if(geteuid() == 0)
    {
        asprintf(&prompt, "%s%s%s%s", user, "@", host, "# ");
    }
    else
    {
        asprintf(&prompt, "%s%s%s%s", user, "@", host, "$ ");
    }

    // commandes internes
    majComInt();
    listeComInt();

    // boucle du terminal
    char *input;
    char *commande_seule;
    char *ptr_input; // Curseur pour strsep

    while(run)
    {
        input = readline(prompt);
        if (!input) break; // Gestion du Ctrl+D
        add_history(input);

        ptr_input = input; // On ne veut pas perdre l'adresse originale pour le free(input)

        // BOUCLE DE SÉQUENCE : On découpe par le point-virgule
        while ((commande_seule = strsep(&ptr_input, ";")) != NULL) 
        {
            // On saute les blocs vides (ex: si l'utilisateur tape "; ;")
            if (commande_seule[0] == '\0') continue;

            // 1. On prépare la copie pour l'analyse (comme avant)
            char *copie = copyString(commande_seule);
            
            // 2. On découpe en mots (ls, -l, etc.)
            analyseCom(copie);

            // 3. On exécute (Interne puis Externe)
            if (NMots > 0) {
                if (!execComInt(NMots, Mots)) {
                    if (!execComExt(Mots)) {
                        fprintf(stderr, "MonShell: %s: commande introuvable\n", Mots[0]);
                    }
                }
            }
            
            free(copie); // On libère après chaque commande de la séquence
        }

        free(input); // On libère la ligne entière après avoir traité tous les ";"
    
    }

    return 0;
}

int analyseCom(char *b)
{
    free(Mots);
    NMots = 0;
    //on copie la chaine pour ne pas que l'adresse ne change sur la chaine originale
    char *copie_comptage = copyString(b);
    char *mot;

    //premier passage pour compter le nombre de mots
    while((mot = strsep(&copie_comptage, " ")) != NULL)
    {
        // si le mot n'est pas vide pour eviter les doubles espaces
        if (mot[0] != '\0') 
        {
            NMots++;
        }
    }
    free(copie_comptage);

    //deuxieme passage pour remplir le tableau de mot
    // +1 pour le NULL a la fin (sert pour execvp)
    Mots = malloc(sizeof(char *) * (NMots + 1));
    int i = 0;
    while((mot = strsep(&b, " ")) != NULL)
    {
        // si le mot n'est pas vide pour eviter les doubles espaces
        if (mot[0] != '\0') 
        {
            Mots[i] = mot;
            i++;
        }
    }
    Mots[i] = NULL;
    return 0;
}

char *copyString(char *s)
{
    int len = strlen(s);
    
    // caractère \0 à la fin d'ou le +1
    char *copie = malloc(sizeof(char) * (len + 1));

    strcpy(copie, s);

    return copie;
}

void ajouteCom(char *nom, cmd_ptr fct) 
{
    if (nbComActual >= NBMAXC) {
        fprintf(stderr, "Erreur : Tableau de commandes internes plein !\n");
        exit(EXIT_FAILURE); 
    }
    
    tabComInt[nbComActual].nom = nom;
    tabComInt[nbComActual].fct = fct;
    nbComActual++;
}

void majComInt(void)
{
    ajouteCom("exit", Sortie);
    ajouteCom("cd", Cd);
    ajouteCom("pwd", Pwd);
    ajouteCom("vers", Vers);
    ajouteCom("beuip", Beuip);
}

void listeComInt(void)
{
    printf("%d commandes internes\n", nbComActual);
    for(int i = 0; i < nbComActual; i++)
    {
        printf("> %s\n", tabComInt[i].nom);
    }
}

int execComInt(int N, char **P)
{
    for(int i = 0; i < nbComActual; i ++)
    {
        if(!strcmp(P[0], tabComInt[i].nom))
        {
            tabComInt[i].fct(N, P);
            return 1;
        }
    }

    return 0;
}

int execComExt(char **P) {
    pid_t pid;
    int status;

    pid = fork();

    if (pid == -1) {
        // impossible de creer un processus fils
        perror("fork");
        return 0;
    } 
    else if (pid == 0) {
        // processus fils
        // si execvp s'execute, elle ne revient jamais ici donc on a pas besoin de
        // gerer le retour en cas de succes
        execvp(P[0], P);
        
        //si ca s'execute ici encore, c'est que ca a echoué
        perror("commande introuvable");
        exit(EXIT_FAILURE);
    } 
    else {
        // processus pere
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            return 0; // la commande externe a échoué
        }
        return 1;
    }
}

int Sortie(int N, char *P[])
{
    // On s'assure de couper les serveurs proprement si l'utilisateur quitte avec 'exit'
    beuip_stop();
    exit(0);
}

int Cd(int N, char *P[])
{
    if (N < 2) {
        // Si on tape juste "cd", on pourrait aller dans HOME, 
        // mais pour l'instant, on va juste dire qu'il manque un argument
        fprintf(stderr, "cd: argument manquant\n");
        return 0;
    }
    if (chdir(P[1]) != 0) {
        perror("cd");
        return 0;
    }
    return 1;
}

int Pwd(int N, char *P[])
{
    char cwd[1024]; // Un tampon pour stocker le chemin
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
    return 1;
}

int Vers(int N, char *P[])
{
    printf("version 3.0\n");
    return 1;
}

int Beuip(int N, char *P[]) {
    if (N < 2) {
        printf("beuip : arguments manquants\n");
        return 0;
    }

    if (strcmp(P[1], "start") == 0) {
        if (N < 3) {
            printf("beuip start <pseudo>\n");
            return 0; 
        }
        beuip_start(P[2]);
    } 
    else if (strcmp(P[1], "stop") == 0) {
        beuip_stop();
    }
    else if (strcmp(P[1], "liste") == 0) {
        commande('3', NULL, NULL);
    }
    else if (strcmp(P[1], "msg") == 0) {
        if (N < 4) {
            printf("Usage: beuip msg <pseudo> <message>\n");
            return 0;
        }
        commande('4', P[3], P[2]);
    }
    else if (strcmp(P[1], "broadcast") == 0) {
        if (N < 3) {
            printf("Usage: beuip broadcast <message>\n");
            return 0;
        }
        commande('5', P[2], NULL);
    }
    // NOUVELLES COMMANDES (Etape 3)
    else if (strcmp(P[1], "ls") == 0) {
        if (N < 3) {
            printf("Usage: beuip ls <pseudo>\n");
            return 0;
        }
        demandeListe(P[2]);
    }
    else if (strcmp(P[1], "get") == 0) {
        if (N < 4) {
            printf("Usage: beuip get <pseudo> <fichier>\n");
            return 0;
        }
        demandeFichier(P[2], P[3]);
    }
    else {
        printf("Commande beuip non reconnue.\n");
    }
    return 1;
}
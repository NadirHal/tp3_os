# Polytech OS User  TP3 (Projet BEUIP)


L'objectif de ce TP est de finaliser notre interpréteur de commande `biceps` en intégrant un système de messagerie et de partage de fichiers distribué en réseau local (BEUIP).

### Ce qui a été implémenté (Structure du code)

- **Multithreading (Partie 1) :** Notre processus père lance deux `pthread` (un pour le serveur UDP de messagerie, un pour le serveur TCP en bonus). Les threads partagent l'accès à notre annuaire. 
- **Liste chaînée dynamique (Partie 2.2) :** `struct elt` gère l'annuaire des utilisateurs connectés (protégée contre les accès concurrents par des `pthread_mutex_t`).
- **Gestion du Broadcast (Consigne 7) :** L'adresse `192.168.88.255` est paramétrée dans un `#define` unique (Partie 2.1 passée selon les consignes du mail).
- ** Partage TCP (Partie 3) :** Implémentation des commandes `ls` et `get` sur le port 9998 en redirigeant les E/S avec `dup2()` et `fork()`.

### Maintenabilité et Sécurité

1. **Compilation stricte :** Code garanti sans _warnings_ grâce aux options `-Wall -Werror` dans le Makefile.
2. **Aucune fuite mémoire :** Le code passe rigoureusement les tests `valgrind` (libération de la readline `clear_history()`, libération de la liste chaînée à l'arrêt, et annulation propre des threads au CTRL+D).
3. **Fonctions concises :** Les routines lourdes (notamment la boucle de `recvfrom`) ont été extraites dans des sous-fonctions pour rester au maximum sous la barre des 20 lignes.

### Commandes pour lancer les tests automatiques:
```bash
make clean
make

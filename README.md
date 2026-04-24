L'objectif de cette étape était de transformer l'architecture du projet pour passer d'un modèle "multi-processus" à un modèle "multi-thread" et implémenter l'échange de fichiers entre utilisateurs.

Ce qui a été réalisé :

 - Multi-threading : Le serveur UDP tourne désormais dans un thread.
 - Gestion de la concurrence : Utilisation de mutex (`pthread_mutex_t`) pour protéger l'accès à la liste chaînée des contacts (annuaire).
 - Adaptation au réseau : Utilisation de `getifaddrs()` pour scanner les interfaces réseau de la machine et envoyer le datagramme UDP de connexion en broadcast sur les bonnes IP (et plus en dur sur l'ancienne IP).
 - Serveur TCP : Création d'un second thread (`serveur_tcp`) qui écoute sur le port 9998 pour le partage de fichiers. Un dossier `reppub/` est créé automatiquement au démarrage du serveur.

Commandes BEUIP intégrées :

 - `start <pseudo>` / `stop` : Lancement et arrêt des threads serveurs.
 - `liste` : Affiche les personnes connectées.
 - `msg <pseudo> <message>` / `broadcast <message>` : Envoi de messages UDP.
 - `ls <pseudo>` : Demande à un utilisateur la liste de ses fichiers via TCP et affiche le résultat (redirection via dup2).
 - `get <pseudo> <fichier>` : Télécharge un fichier depuis le répertoire partagé d'un utilisateur et l'enregistre en local.

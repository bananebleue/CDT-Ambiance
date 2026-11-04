# Installer Spring's Sonar, et revenir à la V15

> Installer un firmware se fait à vos risques. Le FM-1 n'a qu'une zone de mémoire : **changer de firmware efface
> les sons, projets et réglages enregistrés**. Sauvegardez-les d'abord.

## Avant

1. Notez le firmware en place : sur [CDT Compagnon](https://bananebleue.github.io/CDT-Ambiance/), bouton
   « Détecter le FM-1 » (`FM-1_015` = V15 officielle).
2. Sauvegardez vos sons et projets avec l'outil de M-VAVE (éditeur / Workbench) si vous en avez.
3. Téléchargez le firmware officiel V15 (`FM-1.fwsc`, page FM-1 du site de M-VAVE) et gardez-le : c'est votre
   chemin de retour.
4. Choisissez un moment sans partie prévue : jamais juste avant de jouer.

## Installer

1. Récupérez `spring-sonar.fwsc` : artefact `spring-sonar-fwsc` de la dernière exécution réussie de
   *fm1-firmware* sur GitHub (onglet Actions), ou une version publiée.
2. Décompressez le zip pour obtenir `spring-sonar.fwsc`.
3. Dans Chrome ou Edge, sur ordinateur ou sur le téléphone, ouvrez
   [CDT Compagnon](https://bananebleue.github.io/CDT-Ambiance/), section *Installer un firmware* : fermez d'abord
   le Firmware Hub et tout logiciel qui utilise le FM-1, branchez le FM-1 en USB, choisissez le fichier, cochez la
   sauvegarde, puis *Installer sur le FM-1*. Ne débranchez rien pendant l'écriture.
   (Ou, avec Python 3.12 : `py -3.12 tools/fm1_install.py spring-sonar.fwsc`, après
   `py -3.12 -m pip install mido python-rtmidi`.)
4. Le FM-1 redémarre sur l'écran du sonar ; « Détecter le FM-1 » doit répondre `FM-1_960`.

## Revenir au firmware officiel

Dans [CDT Compagnon](https://bananebleue.github.io/CDT-Ambiance/), section *Revenir au firmware officiel* :
1. *Télécharger le firmware officiel* : `FM-1.fwsc`, la dernière version, directement chez M-VAVE (V16 depuis le
   21 octobre 2026). M-VAVE n'autorise pas une page web à le télécharger elle-même, et ce dépôt ne le copie pas.
2. *Choisir FM-1.fwsc* : la page n'accepte que les fichiers officiels non modifiés (empreinte SHA-256 de la V16 ou
   de la V15).
3. *Revenir au firmware officiel* : même installation que pour Spring's Sonar.

Ou le bouton « official V15 » du [FM-1 Firmware Hub](https://thefirmwarehub.com), ou l'outil M-UPGRADE de M-VAVE.

## Si quelque chose se passe mal

- **Il plante :** l'écran rouge montre où (le PC), puis le FM-1 redémarre. Deux plantages au démarrage et il
  passe tout seul en secours USB.
- **Il ne démarre plus :** maintenez **OCT−** en l'allumant : le secours USB, sans son, seulement la mise à jour.
  Installez la V15 depuis le Firmware Hub.
- **Même le secours ne répond plus :** le chargeur d'origine de la puce sait encore se mettre à jour par USB :
  [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) ou
  [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker) (réécrit la V15 officielle, vérifiée).

## À vérifier au premier essai

- l'écran au démarrage, les quatre molettes, ALGORITHM, SELECT, PRESETS ;
- PLAY court / long, REC maintenu, SAVE, OCT− / OCT+, une touche du clavier ;
- le son au haut-parleur et au casque, sans craquement (charge du calcul audio) ;
- l'heure envoyée par CDT Compagnon ;
- l'autonomie sur batterie, sonar en marche, sur 1 h 30 à 3 h.

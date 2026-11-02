# CDT Ambiance

Ambiances sonores pour le jeu de rôle **Les Chants de Tindalos** (Walpurgis Éditions).

Premier module : le **sonar de Spring's Island**. Un ping émis, son pong qui revient, de plus en plus rapprochés jusqu'au bip continu final, avec un compte à rebours réglable en direct pendant la partie.

**Utiliser la page :** https://bananebleue.github.io/CDT-Ambiance/

Elle fonctionne dans un navigateur récent, sur téléphone comme sur ordinateur. Sur Android, Chrome propose de l'installer sur l'écran d'accueil ; elle marche ensuite sans connexion.

## En partie

1. Choisis ou règle un preset : départ différé, fade-in, durée, volume, type de son, réverb, tonalité.
2. **Lancer le sonar.** L'écran reste allumé pendant le sonar.
3. Pendant la scène :
   - **Couper le son** : le décompte continue. Pour le rétablir, appui court pour un retour immédiat, appui long pour un retour en fondu (durée du fade-in).
   - **± Temps** : ajoute ou retire du temps selon le rythme de la partie.
   - **Écouter** : fait entendre un ping et son pong avant le départ, ou une fois au rythme actuel quand le son est coupé.
4. À zéro, le bip continu démarre et le compteur passe en dépassement (+0:42), avec l'heure de fin et l'heure actuelle.
5. **Arrêter** : maintenir le bouton 1 s, pour éviter un arrêt accidentel.

Sur ordinateur : `Espace` = PLAY (maintenir pour un retour en fondu), une lettre = écouter, `R` maintenu = arrêter.

## Réglages

| Réglage | Défaut | Pas |
| --- | --- | --- |
| Départ | 0:00 | 5 s ; 1 min au-delà de 2 min |
| Fade-in | 0:20 | 5 s |
| Durée | 60:00 | 1 min ; 10 s sous 5 min |
| Volume | 70 % | 2 % |
| Réverb générale | 30 % | 2 % |
| Tonalité | 0 | 1 demi-ton |
| Type de son | Coque | — |

Les valeurs ne descendent jamais sous zéro. Les presets et réglages restent enregistrés dans le navigateur.

## Le son

- Le **ping** est un bip avec sa propre réverbération ; le **pong**, son retour, est le même bip, sec.
- L'intervalle entre deux pings va de 8 s à 0,25 s : `I(r) = Imin + (Imax − Imin) · (r/T)^0,7`, où r est le temps restant et T la durée réglée.
- Le pong arrive `min(1,8 s ; 0,45 × I)` après le ping, vers le milieu du cycle.
- Pendant le pong, la réverbération du ping baisse de 7 dB pendant 0,3 s pour qu'il ressorte.

| Type | Hauteur | Caractère | Réverbération du ping |
| --- | --- | --- | --- |
| Abysse | 1 040 Hz | Sinus pur, attaque douce | ~3,3 s |
| Coque | 1 190 Hz | Sinus, attaque franche | ~3 s |
| Métal | 1 034 Hz | Partiel à 1 200 Hz et harmoniques : timbre métallique | ~1,2 s |
| Clic | 1 100 Hz | Clic de bruit à l'attaque | ~2,5 s |
| Sonde | 1 040 Hz | Abysse, moins réverbéré | ~1 s |
| Impulsion | 1 100 Hz | Clic, moins réverbéré | ~0,8 s |

Tous les sons sont synthétisés en direct (Web Audio) ; aucun fichier audio n'est embarqué. Les types s'inspirent de l'analyse de sons de référence de La Sonothèque, voicebosch et freesound community.

## Feuille de route

- [x] Version web (sonar sur téléphone)
- [ ] Compagnon M-VAVE FM-1 : installation du firmware, sauvegarde et retour au firmware d'origine, synchronisation de l'heure, noms des presets (le bouton « Détecter le FM-1 » sert déjà de test Web MIDI)
- [ ] Firmware Spring's Sonar pour le M-VAVE FM-1
- [ ] Autres ambiances (Bath's Astoria…)

## Licence

GPL-3.0 — voir [LICENSE](LICENSE).

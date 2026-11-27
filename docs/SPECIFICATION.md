# Cahier des charges — application OPERA / TWIST

Version 0.4 — 28 octobre 2026

Une application de bureau pour recevoir et émettre les balises OPERA et TWIST : décodage continu depuis la carte son ou des fichiers WAV, émission vers un transceiver avec PTT, en réutilisant le moteur de twistd. Version 0.3 : décisions du 28 octobre intégrées, réglage par porteuse continue (Tune), séquences de balise (trames, porteuses, pauses) et disposition de la fenêtre principale ajoutés après la revue de la maquette. Version 0.4 : précisions sur le waterfall (bande affichée, résolution, étiquettes, graduations) après les premiers essais en réception.

## 1. Objet et utilisateurs

L'application reçoit et émet les balises OOK OPERA (Op05) et TWIST (courte S, longue L), à partir d'un PC relié à un transceiver par sa carte son. Elle remplace, pour ces modes, le logiciel OPERA officiel et la chaîne SpyServer + twistd pour un usage station.

- **Utilisateurs** : radioamateurs qui écoutent les balises VHF/UHF/SHF, ou qui font balise depuis leur station.
- **Phase 1 — tests** : usage par F1HDI et quelques testeurs, pour valider TWIST en conditions réelles.
- **Phase 2 — diffusion publique** : si les résultats sont positifs ; il faut alors un installeur, une documentation et une interface en anglais.
- **Protocoles** : OPERA Op05 et TWIST S/L uniquement. L'architecture doit permettre d'en ajouter d'autres plus tard (PI4…) sans toucher à l'interface.

## 2. Réception

Le décodage tourne en continu sur le flux audio, sans attendre une minute pile : OPERA et TWIST ne sont pas synchrones, une trame peut commencer à n'importe quelle seconde.

**Sources**

- Carte son en direct (entrée USB du transceiver, interface type SignaLink, câble virtuel), mono, 12 kHz ou plus.
- Fichiers WAV : ouverture d'un fichier ou d'un dossier, rejoué à vitesse réelle ou décodé d'un bloc.
- Option ultérieure : fichiers I/Q (large bande), déjà gérés par twistd.

**Décodage continu**

- **Quasi temps réel** : une trame s'affiche **1 à 4 s après sa fin**. Une passe courte toutes les 3 s ne cherche que les trames qui viennent de se terminer (début entre « maintenant − durée − 3 s » et « maintenant − durée », pour chaque protocole) dans les 45 dernières secondes d'audio : la recherche en temps est étroite, la passe dure moins de 0,5 s.
- **Trame en cours** : dès qu'un signal manipulé apparaît (détecteur de manipulation de la recherche large bande, sur les 8 dernières secondes de la zone, vu sur deux passes de suite), sa fréquence est marquée dans le waterfall et la liste des spots montre une ligne « réception en cours », remplacée par le décodage à la fin de la trame. Fonctionne jusque vers −20 dB ; un signal manipulé plus long qu'une trame (identification CW, autre mode) n'est pas signalé.
- **Élimination des doublons** : même protocole, même indicatif, heure de début de trame à moins de 5 s (comme spyfbbclient).
- **Réglages** : fréquence audio centrale et largeur de recherche (défaut 1500 ± 200 Hz), protocoles cherchés, liste d'indicatifs connus (recherche approfondie OPERA), mode étalement (diffusion par la pluie).
- Le décodage ne doit jamais bloquer l'interface ni l'acquisition audio.

**Fenêtre principale** (disposition validée sur la maquette)

De haut en bas, sur toute la largeur de la fenêtre : menu, ligne de l'essentiel (et, sur demande, ligne des informations techniques), waterfall, spots, journal, barre d'état. Le waterfall et le journal sont des volets repliables, et chaque partie peut aussi être détachée dans une fenêtre séparée et redimensionnée ; positions, tailles et état ouvert ou replié sont mémorisés. Le bouton à deux fenêtres à droite de chaque titre (ou Affichage › … dans une fenêtre séparée) détache la partie ; fermer sa fenêtre, ou le même bouton, la remet en place. Une partie détachée est toujours ouverte ; dans la fenêtre principale, la partie restante qui peut grandir prend la hauteur (les spots ; sinon le waterfall, puis le journal). Une position sur un écran qui n'existe plus n'est pas reprise.

1. **Ligne de l'essentiel** (une ligne, retours des testeurs : le haut était trop dense) : horloge UTC en grand, niveau d'entrée (dBFS au survol), décodage actif, fréquence de la radio ; à droite, état de la balise (un clic ouvre la fenêtre Balise), prochaine émission, « Émettre maintenant » ou « ARRÊT · Échap ». Chaque libellé reste accroché à son contrôle : quand la fenêtre est étroite, c'est le couple libellé + contrôle qui passe à la ligne, jamais le texte seul.
2. **Informations techniques** (Affichage › Informations techniques, une seconde ligne) : source audio, protocoles cherchés, zone de recherche, niveau en dBFS, durée de la dernière passe et délai de la prochaine, charge CPU, état audio (fréquence d'échantillonnage, pertes), Tune. Les protocoles se cochent aussi dans le menu Réception, la source et la zone dans les Réglages.
   - Waterfall et spots, tous deux dans la fenêtre principale, se partagent la hauteur (séparation réglable à la souris, gardée ; hauteurs minimales des deux côtés).
   - Le niveau d'entrée est un bargraphe de −60 à 0 dBFS dont la couleur suit la zone : trop bas sous −40 dBFS, bon de −40 à −10, élevé de −10 à −3, saturation au-dessus de −3 (seuils à confirmer, éventuellement réglables dans l'onglet Audio). Les zones restent visibles en fond et un repère marque le maximum récent.
3. **Waterfall** (volet repliable) : spectre défilant de la bande audio, repères de la zone de recherche et des signaux décodés ou en cours, réglages gain / contraste / bande / vitesse. Replié, il garde sa barre de titre et y signale une réception en cours.
   - Bande affichée, au choix : 1000 Hz autour de la fréquence centrale de recherche, ou toute la bande audio de 300 à 2700 Hz (rien d'utile au-delà à travers le filtre BLU). La zone de recherche reste marquée dans les deux cas ; changer de bande ou de fréquence centrale efface l'image.
   - Vitesse : une ligne toutes les 0,2, 0,5 ou 1 s (60 s, 2 min 30 ou 5 min affichées).
   - Chaque ligne est le spectre des 0,7 dernières secondes environ (résolution 1,5 Hz) : une balise y trace un trait d'environ 8 Hz. Les lignes ne suivent pas les chips (0,128 s) ; le début et la fin d'une trame sont flous d'environ 0,7 s.
   - Les couleurs sont relatives au bruit de fond de chaque ligne, pour rester lisibles quel que soit le niveau d'entrée ; le gain décale les couleurs, le contraste resserre la plage affichée (de 40 à 10 dB).
   - Chaque trame décodée est étiquetée « indicatif · protocole » (par exemple « F5ZZO · OPERA 05 », « F1HDI · TWIST L ») à sa fréquence, à la ligne où elle s'est terminée.
   - Axe des fréquences sous l'image : une graduation tous les 50 Hz et une valeur tous les 100 Hz, espacées (200 ou 500 Hz) si la fenêtre est étroite.
4. **Spots** : un tableau, une ligne par décodage — heure UTC de début de trame, protocole et sous-mode, indicatif, locator, SNR, fréquence audio et RF, distance et azimut.
   - Chaque ligne est visible en entier : pas de retour à la ligne dans une cellule, pas de défilement horizontal. Défilement vertical avec en-tête fixe ; la ligne « réception en cours » est en tête, puis le spot le plus récent.
   - « Vider la liste » vide l'affichage seulement (journal CSV et publications MQTT intacts), avec annulation possible juste après.
   - La liste occupe la place libérée quand le waterfall ou le journal sont repliés.
5. **Journal** (volet repliable) : messages techniques (diagnostics du décodeur, erreurs audio ou radio). Replié, il tient sur une ligne : nombre de messages et dernier avertissement.

## 3. Émission

L'application émet une trame OPERA, TWIST S ou TWIST L avec l'indicatif et le locator de la station, par la méthode audio en version 1 ; la manipulation directe sera ajoutée si les premiers utilisateurs en ont besoin.

| Méthode | Principe | Mode du transceiver | Pour | Contre |
| --- | --- | --- | --- | --- |
| **Audio** (défaut) | Un ton audio (1500 Hz par défaut) coupé et rétabli au rythme des chips, avec fronts adoucis de 5 ms contre les claquements | USB | Aucune interface spéciale, comme le logiciel OPERA officiel et WSJT-X | Réglage du niveau audio et de l'ALC nécessaire |
| **Manipulation directe (version 2)** | La ligne de manipulation (KEY) est commandée par la ligne DTR ou RTS d'un port série, comme un manipulateur CW | CW | Porteuse pure, pas de réglage audio | Interface série avec transistor ou optocoupleur |

**Commande d'émission (PTT)**, sur le modèle de WSJT-X :

- VOX (aucune commande) ;
- CAT : commande par le protocole du transceiver, via Hamlib (`rigctld`, lancé par TWIST ou déjà en service sur le réseau) ; PTT « données » (prise arrière / USB) si le transceiver le connaît ;
- DTR ou RTS d'un port série (interfaces type SignaLink, câbles maison).
- Le PTT est commandé une avance réglable (0,2 s) avant l'audio, en suivant la séquence (relâché pendant les pauses qui le relâchent) ; un PTT refusé ou une radio qui ne répond plus arrête l'émission.

**Cycle de balise**

- Trame émise une fois sur demande, ou en cycle : toutes les N minutes à la seconde S (heure PC synchronisée, minutes comptées depuis 00:00 UTC). Le premier chip part à la seconde : la latence de sortie de la carte son est absorbée (silence jusqu'à ce que le premier échantillon atteigne le convertisseur).
- À chaque cycle, l'application déroule une **séquence d'étapes** définie par l'utilisateur, dans l'ordre :
  - **Trame** : OPERA, TWIST S ou TWIST L, avec l'indicatif et le locator de la station ;
  - **Porteuse** : porteuse continue d'une durée fixée (comme le Tune), par exemple pour que les stations à l'écoute mesurent le niveau ou le décalage de fréquence ;
  - **Pause** : silence d'une durée fixée, PTT maintenu ou relâché au choix.
  Exemple : TWIST L, porteuse 10 s, pause 5 s, OPERA. L'interface affiche la durée totale de la séquence et refuse une séquence plus longue que la période du cycle.
- Réception automatique entre deux émissions.
- Sécurités : bouton d'arrêt immédiat, durée d'émission continue maximale (chien de garde, mesurée tant que le PTT reste actif : une séquence dont les pauses maintiennent le PTT compte en entier), PTT relâché à la fermeture ou en cas d'erreur.
- Cadencement des chips sur l'horloge audio (méthode audio) ou une horloge haute résolution (manipulation directe) : 128 ms par chip sans dérive sur 41 s.

**Réglage (Tune)**

Un bouton « Tune » émet une porteuse continue pour vérifier et régler la chaîne d'émission (niveau audio, ALC, puissance, ROS), comme dans WSJT-X.

- Même chemin qu'une trame : ton d'émission (1500 Hz par défaut), niveau de sortie et commande PTT de l'émission ; le réglage fait en Tune vaut donc pour la balise.
- Arrêt automatique après une durée réglable (30 s par défaut). Le chien de garde (durée d'émission maximale) s'applique aussi : même sans arrêt automatique, la porteuse ne reste jamais émise au-delà.
- Arrêt par un second appui sur Tune, ou par le bouton d'arrêt immédiat (touche Échap), comme une trame. Pendant le Tune, l'interface affiche le même état d'émission (bandeau, temps écoulé).
- Si une émission du cycle de balise tombe pendant un Tune, elle attend la fin du Tune (à confirmer par les testeurs).

**Outils annexes**

- Générer un fichier WAV de test d'une trame, avec bruit et SNR réglables.
- Afficher et exporter les chips d'une trame (0/1 et hexadécimal), pour vérifier une balise comme FbBalise.

## 4. Spots et rapports

Chaque décodage est gardé localement et peut être envoyé à la base fbbspots (spots.f6fbb.org, avec la clé de la station, demandée par email), ou publié en MQTT vers un serveur de l'utilisateur, dans le même format que spyfbbclient.

- **Journal local** : fichier CSV par jour UTC, `twist-AAAA-MM-JJ.csv` (date, heure de début de trame, protocole et sous-mode, indicatif, locator, SNR, fréquences audio et RF, distance, azimut, « deep », source), séparateur `;` et point décimal, répertoire configurable ; une ligne écrite dès le décodage.
- **Publication MQTT** (option) : même topic (`spyfbbclient/<hôte>/spot`) et même JSON que spyfbbclient, toutes les valeurs en texte (`date` = début de trame à la seconde, `mode` et `submode` — par exemple TWIST / L —, `frequency` RF en Hz, `snr`, `payload`, `callsign`, `locator`, `deep`, `reporter`, `rgrid`), donc traité sans changement par spyspots, validation des indicatifs inconnus comprise. Seuls les spots reçus en direct sont publiés ; file d'attente si le serveur est injoignable.
- **Fréquence RF** = fréquence de la radio (saisie, ou lue par CAT) + fréquence audio du signal.
- **Enregistrement audio** (option) : garder les WAV des passes qui ont décodé quelque chose, pour analyse (comme KEEPDIR).

## 5. Configuration

Une boîte de réglages à onglets, enregistrée dans un fichier JSON lisible dans le profil de l'utilisateur (`%APPDATA%\TWIST\twist.json` sous Windows). Les valeurs changées dans la fenêtre principale (source, protocoles, zone, fréquence de la radio, waterfall, volets, taille et position de la fenêtre) y sont aussi enregistrées.

| Onglet | Réglages |
| --- | --- |
| Station | Indicatif, locator (4 ou 6 caractères), indicatifs connus |
| Audio | Périphérique d'entrée et de sortie, fréquence d'échantillonnage, niveau de sortie, fréquence du ton d'émission |
| Radio | Modèle et port CAT (Hamlib), méthode PTT (VOX, CAT, DTR, RTS) |
| Décodage | Protocoles cherchés, fréquence centrale et largeur de recherche, étalement (Hz), période des passes, retard de la chaîne de réception (s, recule les heures de début : SDR, réseau, câble virtuel) |
| Balise | Séquence d'étapes (trames, porteuses, pauses), période et seconde de départ, durée maximale d'émission continue, arrêt automatique du Tune |
| Rapports | Répertoire du journal, serveur MQTT (adresse, port, identifiants), conservation des WAV |
| Interface | Langue (français / anglais), thème clair ou sombre, fenêtres ouvertes au démarrage, volets ouverts ou repliés, réglages du waterfall (gain, contraste, bande, vitesse) |

Plusieurs profils (par exemple 2 m et 70 cm, ou deux transceivers) pourront être ajoutés ensuite ; le format du fichier doit le permettre.

## 6. Architecture proposée

Quatre couches empilées : tout ce qui touche au signal et à la radio est en C, dans une bibliothèque partagée avec twistd ; l'interface en C# n'en connaît qu'une API simple.

```
 Interface (C# .NET 8 + Avalonia)   fenêtres, réglages, MVVM
              │
 Services C#                        planificateur des passes, journal, MQTT, profils
              │  P/Invoke
 libtwist (C)                       moteur twistd (core/), PortAudio, Hamlib
              │
 Matériel                           carte son, transceiver (CAT, PTT)
```

Chaque couche ne parle qu'à celle du dessous : on peut changer l'interface sans toucher au décodage, et une amélioration du moteur profite à la fois à l'application et à twistd.

**API C du cœur (esquisse)** — peu de fonctions, des types simples :

1. `tw_decode(échantillons, n, fs, réglages, début_min, début_max, spots[])` : une passe de décodage sur un bloc audio, limitée aux trames dont le début tombe dans la fenêtre donnée (le cœur de twistd, sans état ; l'option -d existe déjà).
2. `tw_encode(type, indicatif, locator, chips[])` : les chips d'une trame (twist\_encoder).
3. `tw_audio_open / tw_audio_last(secondes) / tw_audio_play(chips, ton)` : capture continue dans un tampon circulaire, émission cadencée par l'horloge audio.
4. `tw_rig_open / tw_rig_ptt(on) / tw_rig_key(on) / tw_rig_freq()` : CAT, PTT et manipulation via Hamlib ou le port série.

Le planificateur (C#) appelle `tw_audio_last(45)` toutes les 3 s puis `tw_decode` dans un fil séparé : l'interface et l'acquisition ne sont jamais bloquées.

**Choix de la technologie d'interface**

| Option | Plateformes | Pour | Contre |
| --- | --- | --- | --- |
| **C# .NET 8 + Avalonia** (recommandée) | Windows, Linux, macOS | C# que tu connais ; XAML proche de WPF ; MVVM qui sépare naturellement écran et logique | Un framework à découvrir (proche de WPF) |
| C# + WinForms | Windows seulement | Déjà maîtrisé, le plus rapide à démarrer | Pas de multi-plateforme ; mélange facile écran et logique |
| C++ + Qt 6 (comme WSJT-X) | Windows, Linux, macOS | Performant, éprouvé en radioamateur | Qt à apprendre ; C++ plus long à maintenir pour une interface |

Recommandation : C# .NET 8 + Avalonia au-dessus de libtwist. Comme l'audio et la radio sont dans la couche C (PortAudio et Hamlib, tous deux multi-plateformes), l'interface reste mince ; si le multi-plateforme est abandonné, une interface WinForms se brancherait sur la même API sans rien changer en dessous.

## 7. Exigences non fonctionnelles

- **Plateformes** : Windows 10 et 11 (64 bits) en premier ; Linux et macOS possibles ensuite sans réécrire l'application.
- **Performances** : une trame affichée au plus 4 s après sa fin ; une passe courte (trois protocoles) en moins de 0,5 s sur un PC courant, dans un fil séparé ; pas de perte d'échantillons audio pendant le décodage.
- **Fiabilité en émission** : le PTT n'est jamais laissé actif (arrêt, plantage, fermeture), chien de garde indépendant de l'interface.
- **Langue** : interface en français et en anglais (fichiers de traduction), documentation dans les deux langues pour la phase 2.
- **Installation** : un installeur Windows ou une archive « portable », sans droits administrateur si possible ; dépendances (FFTW, audio, Hamlib) incluses.
- **Maintenance** : couches séparées (section 6), le moteur de décodage partagé avec twistd sans copie du code, tests automatiques du moteur (fichiers de référence, égalité avec twistenc).
- **Licence** : open source, GPL v3, dépôt public sur GitHub. C'est aussi une obligation de fait : FFTW et le code OPERA dérivé des sources de F4GCB et 7L1RLL sont sous GPL ; Hamlib (LGPL) et PortAudio (MIT) sont compatibles.
- **Nom** : TWIST. Une recherche rapide ne montre aucun logiciel radioamateur de ce nom ; avant la diffusion publique, vérifier les marques déposées (INPI, EUIPO) en classe logiciel.
- **OPERA** : le nom n'est utilisé que pour dire la compatibilité (« décode aussi les balises OPERA »), avec mention de son auteur, EA5HVK. Le décodeur repose uniquement sur les sources ouvertes publiées (F4GCB, 7L1RLL, rpitx), sans code ni ressource du logiciel officiel. Par prudence (le logiciel officiel est sous copyright et son auteur n'a pas publié de spécification), le code source ne contient pas le nom : fichiers, fonctions et types « opera\* » du moteur sont renommés en « tw\* » avant la publication sur GitHub. Le nom OPERA n'apparaît que dans l'interface et la documentation, comme protocole compatible.

## 8. Hors périmètre de la première version

- Autres protocoles (PI4, WSPR, FT8…) : l'architecture les permet, mais ils ne sont pas prévus.
- Réception I/Q large bande en direct (SDR) : twistd et spyfbbclient restent l'outil pour cela.
- Carte géographique des spots : le site fbbalise la fournit déjà (base fbbspots).
- Envoi vers PSKReporter ou d'autres réseaux.
- Pilotage de la fréquence de la radio (seule la lecture par CAT est prévue).
- Manipulation directe (ligne KEY) : après les essais de la version 1 avec 2 ou 3 utilisateurs, si le besoin est confirmé.
- Sous-modes OPERA autres qu'Op05 (Op1 à Op32, décamétrique).
- OmniRig : Hamlib seul.

## 9. Critères de recette

- [ ] Le fichier réel F5ZZO est décodé depuis un WAV avec le même résultat que twistd (indicatif, SNR à 1 dB, DT à 0,1 s).
- [ ] Un fichier mélangeant OPERA, TWIST S et TWIST L (généré par gen\_test\_wav) donne les trois spots.
- [ ] En direct, chaque trame s'affiche au plus 4 s après sa fin (mesuré sur 20 trames rebouclées).
- [ ] En direct, la sortie audio de l'application rebouclée sur son entrée (câble virtuel) décode chaque trame émise, quel que soit l'instant de départ.
- [ ] Les chips émis sont identiques à ceux de twistenc pour 10 indicatifs et locators.
- [ ] Une heure d'écoute sans signal ne produit aucun faux décodage, et aucune perte d'échantillons audio.
- [ ] Le PTT est relâché dans tous les cas : arrêt, fin du chien de garde, fermeture, erreur de la carte son.
- [ ] Une séquence trame + porteuse + pause + trame est émise avec les durées réglées, au départ prévu, et l'arrêt immédiat l'interrompt à n'importe quelle étape.
- [ ] Le Tune émet une porteuse continue au ton et au niveau de l'émission, et s'arrête seul à la fin de la durée réglée, par un second appui, par Échap et par le chien de garde.
- [ ] Un spot publié en MQTT est enregistré correctement par spyspots.

## 10. Questions ouvertes

- [x] Technologie de l'interface : C# .NET 8 + Avalonia.
- [x] Contrôle radio : Hamlib seul.
- [x] Émission en version 1 : audio seulement ; manipulation directe jugée après les essais.
- [x] Nom : TWIST, sous réserve de la vérification des marques.
- [x] Licence : open source GPL v3, dépôt GitHub.
- [x] Sous-modes OPERA : Op05 seulement en version 1.
- [x] OPERA dans le code : renommé en « tw\* » ; le nom reste dans l'interface et la documentation (section 7).
- [x] Indicateur « trame en cours » : souhaitable en version 1, non bloquant.
- [ ] Alternance d'un cycle à l'autre (par exemple deux séquences A et B émises à tour de rôle) : à décider après les premiers essais ; la séquence d'un cycle peut déjà enchaîner plusieurs types de trame.

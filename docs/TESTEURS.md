TWIST Console — guide des testeurs
==============================

Merci de tester TWIST Console ! Cette version reçoit et émet les balises
OOK OPERA (Op05) et TWIST (TWIST S, TWIST L), ainsi que les balises PI4
(trame PI4, CW et porteuse), par la carte son, pilote la radio par
Hamlib et peut publier les spots en MQTT (format spyfbbclient). Outils ›
WAV de test fabrique des enregistrements de test au SNR voulu.
Le mode d'emploi complet est le guide de l'utilisateur (Aide › Guide de
l'utilisateur, ou menu Démarrer › TWIST Console).


Installation
------------

1. Lancer TWIST-Console-<version>-setup.exe. Windows 10 ou 11, 64 bits.
   Le programme n'est pas encore signé : si Windows SmartScreen affiche
   « Windows a protégé votre ordinateur », cliquer sur « Informations
   complémentaires » puis « Exécuter quand même ».
2. L'installation se fait pour votre compte, sans droits administrateur
   (ou pour tous les utilisateurs si vous le choisissez).
3. Menu Démarrer › TWIST Console. Si la fenêtre reste vide ou noire (pilote
   graphique), utiliser le raccourci « TWIST Console (affichage logiciel) ».

Une mise à jour s'installe par-dessus : les réglages sont gardés. La
désinstallation ne supprime ni les réglages ni le journal. Depuis la
0.25.0, TWIST Console annonce lui-même une nouvelle version (bandeau au-dessus
des spots) et l'installe sur demande (bouton Installer) : il se ferme et
se relance tout seul.

Connexion lente (4G) : si TWIST Console est déjà installé, la page de la
version propose aussi TWIST-Console-<version>-mise-a-jour.exe, quelques Mo
au lieu d'environ 38 : il ne remplace que le programme et le moteur, au
même endroit. Il refuse un ordinateur sans TWIST Console, ou avec une
version trop ancienne : prendre alors le -setup.exe complet.

Mac (version expérimentale) : Mac à processeur Intel, macOS 10.15
(Catalina) ou plus récent.
1. Ouvrir TWIST-Console-<version>-mac-intel.dmg et glisser TWIST Console dans
   Applications (ou décompresser le .zip).
2. Le programme n'est pas signé par Apple : la première fois, clic droit
   (ou Ctrl-clic) sur TWIST Console › Ouvrir, puis Ouvrir. Si macOS refuse
   encore : Préférences Système › Sécurité et confidentialité › « Ouvrir
   quand même ».
3. Au premier démarrage de la réception, macOS demande l'accès au
   micro (l'entrée audio) : répondre OK. Sinon, rien n'est reçu ; à
   corriger dans Préférences Système › Sécurité et confidentialité ›
   Confidentialité › Micro.
Sous Catalina et plus ancien, l'affichage se fait par le processeur
(beaucoup de ces Mac n'ont pas l'accélération graphique de macOS récent).
Radio : rigctld et rigctl sont fournis avec le programme.
Recevoir le son d'un SDR web (Firefox…) : macOS n'a pas de câble audio
virtuel ; installer BlackHole 2ch (gratuit, existential.audio/blackhole),
redémarrer, puis dans Configuration audio et MIDI : « + » › Créer un
périphérique à sorties multiples, cocher Sortie intégrée (en premier)
et BlackHole 2ch (correction de dérive sur BlackHole), clic droit ›
Utiliser ce périphérique pour la sortie audio. Dans TWIST Console (relancé) :
Source › BlackHole 2ch ; régler le niveau avec le volume du SDR. Un SDR
web retarde le son de 1 à 3 s : Réglages › Décodage › Retard de la
chaîne de réception.


Premier lancement
-----------------

Fichier › Réglages (le profil, en haut, regroupe carte son, radio,
décodage et balise : « Nouveau… » en crée un autre, par exemple 70 cm ;
on passe ensuite de l'un à l'autre dans la barre de titre) :

- Station : indicatif et locator (nécessaires pour émettre et pour les
  distances).
- Audio : entrée (sortie audio du récepteur) et sortie (entrée audio de
  l'émetteur).
- Radio :
  - CAT : « Hamlib (rigctld lancé par TWIST) », choisir le transceiver
    en tapant une partie de son nom, le port COM et la vitesse, puis
    « Tester la liaison » ; ou « rigctld déjà lancé » si un autre
    programme partage déjà la radio.
  - PTT : VOX, CAT, DTR ou RTS ; « Tester le PTT (2 s) » SUR CHARGE
    FICTIVE ou antenne adaptée.
  - Fréquence : lue par CAT, ou saisie.
- Décodage : centre et largeur de la zone de recherche (1500 ± 200 Hz) ;
  PI4 (à cocher, aussi dans le menu Réception), dans sa propre zone
  (porteuse 800 ± 150 Hz).
  « Retard de la chaîne de réception » : si les heures de début sont en
  retard d'une seconde (SDR, câble virtuel), mettre 1.
- Rapports : journal CSV (Documents\TWIST) ; cocher « Conserver les WAV
  des passes qui ont décodé » pendant les essais ; « Envoyer les spots à
  fbbspots » : « Demander une clé… » (votre adresse email), puis coller
  la clé reçue par email. MQTT seulement vers votre propre serveur.
- Interface : thème (sombre, clair, comme Windows) et langue (français,
  English ; la langue change au redémarrage, proposé tout de suite) ;
  son à la réception d'un spot (activé par défaut, sur la sortie son par
  défaut du système : à décocher si elle va à l'émetteur en VOX).


Ce qu'il faut essayer
---------------------

Réception
- Les trames reçues apparaissent dans la liste des spots une seule fois,
  avec l'heure de début, l'indicatif, le SNR et la fréquence.
- Le waterfall montre les signaux ; chaque trame décodée y est
  étiquetée à sa fin.
- Le niveau d'entrée reste dans la zone verte (−40 à −10 dBFS).
- PI4 (Réception › PI4) : chaque trame une fois, juste après sa fin ;
  une balise qui alterne indicatif et « /locator » : la ligne du locator
  porte l'indicatif de la minute précédente. Comparer avec pi4d.

Émission (toujours sur charge fictive ou antenne adaptée)
- Fenêtre Balise : « Tune » donne une porteuse ; régler le niveau de
  sortie pendant le Tune jusqu'au tout début de l'action de l'ALC.
- « Émettre une fois », puis « Démarrer le cycle » : la séquence doit
  partir à la seconde indiquée, le PTT monter avant l'audio et retomber
  à la fin.
- Échap ou le bouton rouge arrête tout immédiatement.
- Si possible, faire décoder votre émission par une autre station
  (TWIST Console, twistd ou le logiciel OPERA) et comparer l'heure.

Relecture d'un WAV
- Fichier › Rejouer un fichier WAV… (ou Source › Fichier WAV…) : une
  bande « Lecture du fichier » montre l'avancement (barre, temps,
  pourcentage) et le nombre de trames décodées. La lecture se fait en
  temps réel ; « Lecture rapide » va aussi vite que le décodage.
  « Revenir à l'entrée audio » reprend la réception par la carte son.

Affichage
- Le bouton à deux fenêtres, à droite du titre du waterfall, des spots
  et du journal, met cette partie dans une fenêtre à part (par exemple
  sur un deuxième écran) ; fermer cette fenêtre la remet en place. Au
  redémarrage, chaque fenêtre revient au même endroit.

Locators
- Une trame OPERA 05 ou TWIST S d'une station connue montre son locator
  en minuscules et en italique grisé (liste des Réglages › Station,
  trame TWIST L déjà reçue, ou fbbspots) ; le survol dit d'où il vient.
  Un locator reçu dans la trame est en majuscules.

Indicatifs douteux
- Un indicatif « fantôme » (préfixe non utilisé par les radioamateurs,
  ou locator TWIST L hors du pays de l'indicatif) est grisé et suivi de
  « ? », la raison au survol ; il n'est pas envoyé à fbbspots. Clic
  droit › Supprimer la ligne (ou Suppr) le retire de la liste. Signaler
  tout vrai indicatif marqué douteux (colonne « douteux » du CSV).

Radio
- La fréquence en haut de la fenêtre suit celle du transceiver.
- Le PTT par CAT fonctionne ; sinon, essayer sans « PTT données ».

fbbspots
- Vos spots arrivent sur spots.f6fbb.org avec le bon indicatif, la
  bonne fréquence et la bonne heure ; le journal du programme dit
  « fbbspots : spots envoyés ».
- Couper le réseau un moment : les spots attendent, puis partent.


Que renvoyer
------------

Pour chaque problème : ce que vous avez fait, ce que vous attendiez, ce
qui s'est passé, une capture d'écran si possible, et surtout le
rapport :

  Aide › Créer un rapport pour les testeurs

Il fabrique un seul fichier zip (Documents\TWIST\rapports) et l'affiche
dans l'explorateur : journal de la session, fichier d'erreurs, réglages
(sans le mot de passe MQTT), versions, cartes son, spots du jour et le
dernier WAV conservé. Il suffit de l'envoyer.

Pour un problème de décodage (trame manquée, faux décodage, mauvaise
heure) : le WAV correspondant, dans Documents\TWIST\wav\<date>
(Fichier › Dossier des WAV conservés), nommé par l'heure UTC et la
fréquence. Une trame manquée n'a pas de WAV (seules les passes qui ont
décodé sont gardées) : un enregistrement fait par un autre logiciel
convient alors.

Indiquer aussi le transceiver, l'interface audio (carte son USB,
SignaLink, câble virtuel, SDR…) et la version de TWIST Console (Aide ›
À propos).


Contenu du dossier installé
---------------------------

- Twist.exe : l'application.
- hamlib\ : rigctld et rigctl (Hamlib), utilisés pour le CAT.
- libtwcore.dll, libtwist.dll : le moteur (audio, émission ; TWIST) ;
  libopera.dll et libpi4.dll si OPERA et PI4 ont été choisis à
  l'installation (cochés par défaut).
- outils\ : les décodeurs de fichiers WAV en ligne de commande, twistd
  (TWIST), operad (OPERA) et pi4d (PI4), ces deux-là avec leur composant.
- LICENSE : licence GNU GPL version 3.

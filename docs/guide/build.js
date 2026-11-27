// build.js - TWIST Console, guide de l'utilisateur (Word), with the docx
// package of npm, from the screenshots of app/Twist.Screenshots:
//   node docs/guide/build.js SCREENSHOTS_FOLDER OUTPUT_FOLDER
// then python3 docs/guide/topdf.py OUTPUT_FOLDER/TWIST-Console-Guide-utilisateur.docx
// (see docs/guide/README.md)
const fs = require('fs');
const path = require('path');
const {
  Document, Packer, Paragraph, TextRun, ImageRun, HeadingLevel, AlignmentType,
  Table, TableRow, TableCell, WidthType, ShadingType, BorderStyle, LevelFormat,
  TableOfContents, Header, Footer, PageNumber, PageBreak, TabStopType,
} = require('docx');

const SHOTS = path.resolve(process.argv[2] || 'screenshots');
const OUT = path.resolve(process.argv[3] || '.');
// the version of the application
const VERSION = fs.readFileSync(path.join(__dirname, '../../app/Twist.App/Twist.App.csproj'), 'utf8')
  .match(/<Version>([^<]+)<\/Version>/)[1];
const ACCENT = '1F6F8B';
const TEXT_W = 9638; // A4, marges 2 cm : 21 - 4 = 17 cm

// **gras** et `code` dans un texte
function runs(text, base = {}) {
  const out = [];
  const re = /(\*\*[^*]+\*\*|`[^`]+`)/g;
  let last = 0, m;
  while ((m = re.exec(text))) {
    if (m.index > last) out.push(new TextRun({ text: text.slice(last, m.index), ...base }));
    const t = m[0];
    if (t.startsWith('**')) out.push(new TextRun({ text: t.slice(2, -2), bold: true, ...base }));
    else out.push(new TextRun({ text: t.slice(1, -1), font: 'Consolas', size: 19, ...base }));
    last = m.index + t.length;
  }
  if (last < text.length) out.push(new TextRun({ text: text.slice(last), ...base }));
  return out;
}
const P = (t, o = {}) => new Paragraph({ children: runs(t), spacing: { after: 120 }, ...o });
const H1 = (t, brk = true) => new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun(t)], pageBreakBefore: brk });
const H2 = t => new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun(t)] });
const H3 = t => new Paragraph({ heading: HeadingLevel.HEADING_3, children: [new TextRun(t)] });
const B = (t, level = 0) => new Paragraph({ children: runs(t), numbering: { reference: 'bullets', level }, spacing: { after: 60 } });
let numRef = 0;
function N(items) {   // une liste numérotée qui repart de 1
  const ref = 'num' + (numRef++);
  numberingConfigs.push({ reference: ref, levels: [{ level: 0, format: LevelFormat.DECIMAL, text: '%1.', alignment: AlignmentType.LEFT,
    style: { paragraph: { indent: { left: 567, hanging: 340 } } } }] });
  return items.map(t => new Paragraph({ children: runs(t), numbering: { reference: ref, level: 0 }, spacing: { after: 60 } }));
}
const numberingConfigs = [];

function note(t, kind = 'info') {
  const color = kind === 'warn' ? 'C0392B' : ACCENT;
  const fill = kind === 'warn' ? 'FBEDEA' : 'E8F2F5';
  return new Paragraph({
    children: runs(t),
    shading: { type: ShadingType.CLEAR, color: 'auto', fill },
    border: { left: { style: BorderStyle.SINGLE, size: 18, color, space: 8 } },
    indent: { left: 180, right: 180 },
    spacing: { before: 120, after: 180 },
  });
}

let figNo = 0;
function fig(file, widthPx, caption) {
  const buf = fs.readFileSync(path.join(SHOTS, file));
  const w = buf.readUInt32BE(16), h = buf.readUInt32BE(20);
  const width = widthPx, height = Math.round(h * widthPx / w);
  figNo++;
  return [
    new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 120, after: 60 }, keepNext: true,
      children: [new ImageRun({ type: 'png', data: buf, transformation: { width, height },
        altText: { title: caption, description: caption, name: file } })] }),
    new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 240 },
      children: [new TextRun({ text: `Figure ${figNo} — ${caption}`, italics: true, size: 18, color: '555555' })] }),
  ];
}

function table(cols, rows) {   // cols: [[titre, largeur dxa]...]
  const border = { style: BorderStyle.SINGLE, size: 4, color: 'BFC7CC' };
  const borders = { top: border, bottom: border, left: border, right: border };
  const cell = (t, w, head) => new TableCell({
    borders, width: { size: w, type: WidthType.DXA },
    shading: head ? { type: ShadingType.CLEAR, color: 'auto', fill: 'DCE9EE' } : undefined,
    margins: { top: 60, bottom: 60, left: 100, right: 100 },
    children: [new Paragraph({ children: runs(t, head ? { bold: true } : {}) })],
  });
  return new Table({
    width: { size: cols.reduce((a, c) => a + c[1], 0), type: WidthType.DXA },
    columnWidths: cols.map(c => c[1]),
    rows: [
      new TableRow({ tableHeader: true, children: cols.map(c => cell(c[0], c[1], true)) }),
      ...rows.map(r => new TableRow({ children: r.map((t, i) => cell(t, cols[i][1], false)) })),
    ],
  });
}
const gap = () => new Paragraph({ spacing: { after: 120 }, children: [] });

// ------------------------------------------------------------------ contenu

const titlePage = [
  new Paragraph({ spacing: { before: 2600 }, alignment: AlignmentType.CENTER,
    children: [new TextRun({ text: 'TWIST Console', bold: true, size: 72, color: ACCENT })] }),
  new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 240 },
    children: [new TextRun({ text: "Guide de l'utilisateur", size: 40 })] }),
  new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 1200 },
    children: [new TextRun({ text: 'Réception et émission de balises : TWIST S, TWIST L, OPERA 05, PI4', size: 24, color: '555555' })] }),
  ...fig('main-replay.png', 560, 'La fenêtre principale après la relecture d\'un enregistrement').slice(0, 1),
  new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 1000 },
    children: [new TextRun({ text: `Version ${VERSION} · ${new Date().toLocaleDateString('fr-FR', { month: 'long', year: 'numeric' })}`, size: 22 })] }),
  new Paragraph({ alignment: AlignmentType.CENTER,
    children: [new TextRun({ text: 'Logiciel libre (GPL v3) · F6FBB', size: 22, color: '555555' })] }),
];
figNo = 0;   // l'image de couverture n'est pas numérotée

const toc = [
  new Paragraph({ pageBreakBefore: true, children: [new TextRun({ text: 'Sommaire', bold: true, size: 32, color: ACCENT })], spacing: { after: 240 } }),
  new TableOfContents('Sommaire', { hyperlink: true, headingStyleRange: '1-2' }),
];

const intro = [
  H1('1. Présentation'),
  P('TWIST Console reçoit et émet des balises en manipulation par tout ou rien (OOK) par la carte son de l\'ordinateur, reliée à un récepteur ou à un émetteur en BLU (USB). Il décode trois types de trames :'),
  B('**OPERA 05** : la trame classique du mode OPERA (Op05), indicatif seul.'),
  B('**TWIST S** : trame courte, indicatif seul.'),
  B('**TWIST L** : trame longue, indicatif et locator ; la distance et l\'azimut de la station sont alors calculés.'),
  P('Il permet aussi de :'),
  B('afficher le spectre reçu dans un waterfall, avec les trames décodées étiquetées ;'),
  B('tenir un journal des spots (fichier CSV, un par jour UTC) et les envoyer à fbbspots (spots.f6fbb.org), ou par MQTT à votre propre serveur au format de spyfbbclient ;'),
  B('conserver les enregistrements (WAV) des passes qui ont décodé et les relire ;'),
  B('émettre une balise : une séquence de trames, de porteuses et de pauses, une fois ou en cycle à heure fixe ;'),
  B('piloter le transceiver par Hamlib : fréquence lue par CAT, commutation émission / réception (PTT).'),
  P('Le programme fonctionne sous Windows 10 et 11 (64 bits) et sur les Mac à processeur Intel depuis macOS 10.15 (Catalina). Les heures sont toujours en UTC ; l\'horloge de l\'ordinateur doit être synchronisée (à la seconde près) pour que les heures des trames et le départ des émissions soient justes.'),
  H2('Comment se fait le décodage'),
  P('Toutes les **3 secondes** (une « passe »), le programme analyse les **45 dernières secondes** d\'audio dans la zone de recherche (par défaut **1500 ± 200 Hz**). Une trame est donc décodée quelques secondes après sa fin. Une même trame vue par plusieurs passes n\'est affichée qu\'une fois : même protocole, même indicatif, heure de début à moins de 5 s. Pendant qu\'une trame arrive, le waterfall et l\'en-tête de la liste des spots signalent « réception en cours ».'),
];

const install = [
  H1('2. Installation'),
  H2('Windows'),
  ...N([
    'Lancer **TWIST-Console-' + VERSION + '-setup.exe**.',
    'Le programme n\'est pas encore signé : si Windows SmartScreen affiche « Windows a protégé votre ordinateur », cliquer sur **Informations complémentaires** puis **Exécuter quand même**. Si le navigateur bloque le téléchargement, choisir « Conserver ».',
    'L\'installation se fait pour votre compte, sans droits d\'administrateur (ou pour tous les utilisateurs si vous le choisissez).',
    'Choisir les protocoles : TWIST (TWIST S, TWIST L) est toujours installé ; **OPERA** et **PI4** sont cochés par défaut, à décocher si on n\'en veut pas (installation personnalisée).',
    'Démarrer par le menu Démarrer › **TWIST Console**.',
  ]),
  note('Si la fenêtre reste vide ou noire (pilote graphique), utiliser le raccourci **TWIST Console (affichage logiciel)** : l\'affichage se fait alors par le processeur.'),
  P('Une mise à jour s\'installe par-dessus l\'ancienne version : les réglages sont gardés. La désinstallation (Paramètres › Applications) ne supprime ni les réglages ni le journal. L\'installation contient aussi rigctld et rigctl (Hamlib) et, dans le dossier `outils`, les décodeurs de fichiers WAV en ligne de commande : twistd (TWIST), operad (OPERA) et pi4d (PI4). Un protocole non installé n\'apparaît nulle part dans le programme ; pour l\'ajouter, relancer l\'installation et le cocher. TWIST Console remplace TWIST-OOK : installé par-dessus, il en garde les réglages et le dossier.'),
  H2('Mac'),
  P('Version pour Mac à processeur Intel, macOS 10.15 (Catalina) ou plus récent.'),
  ...N([
    'Ouvrir **TWIST-Console-' + VERSION + '-mac-intel.dmg** et glisser TWIST Console dans le dossier Applications (ou décompresser le .zip).',
    'Le programme n\'est pas signé par Apple : la première fois, faire un **clic droit** (ou Ctrl-clic) sur TWIST Console › **Ouvrir**, puis Ouvrir. Si macOS refuse encore : Préférences Système › Sécurité et confidentialité › **Ouvrir quand même**.',
    'Au premier démarrage de la réception, macOS demande l\'**accès au micro** (c\'est ainsi que macOS appelle toute entrée audio) : répondre OK. Sans cet accès, rien n\'est reçu ; il se rétablit dans Préférences Système › Sécurité et confidentialité › Confidentialité › Micro.',
  ]),
  P('Sous Catalina et les versions plus anciennes, l\'affichage se fait par le processeur : beaucoup de ces Mac n\'ont pas l\'accélération graphique des macOS récents. rigctld et rigctl (Hamlib) sont fournis dans l\'application.'),
  H2('Où sont les fichiers'),
  table([['Contenu', 3000], ['Windows', 3319], ['Mac', 3319]], [
    ['Réglages (twist.json)', '%APPDATA%\\TWIST', 'chemin affiché en bas de la fenêtre Réglages'],
    ['Journal CSV des spots', 'Documents\\TWIST', 'Documents/TWIST'],
    ['WAV conservés', 'Documents\\TWIST\\wav', 'Documents/TWIST/wav'],
    ['Erreurs du programme (twist-error.log)', '%LOCALAPPDATA%\\TWIST', '~/Library/Application Support/TWIST'],
  ]),
  P('Le dossier du journal se change dans Réglages › Rapports ; les WAV sont toujours dans son sous-dossier `wav`.', { spacing: { before: 120, after: 120 } }),
];

const settings = [
  H1('3. Premiers réglages'),
  P('Ouvrir **Fichier › Réglages…**. Les onglets sont à gauche ; **Enregistrer** applique tout de suite (la carte son est rouverte si l\'entrée a changé), **Annuler** abandonne. Un champ incorrect est signalé et son onglet affiché.'),
  ...fig('settings.png', 470, 'Réglages, onglet Station'),
  H2('Profils'),
  P('Les onglets **Audio, Radio, Décodage et Balise** appartiennent à un **profil** (par exemple « 2 m » et « 70 cm », ou un profil par transceiver). **Station, Rapports et Interface** sont communs à tous les profils.'),
  B('**Nouveau…** : donner un nom, puis partir d\'une copie du profil courant ou des réglages par défaut.'),
  B('**Renommer…** et **Supprimer** (demandé avant) agissent sur le profil choisi.'),
  B('Dès qu\'il y a plusieurs profils, une liste en haut à droite de la fenêtre principale permet de passer de l\'un à l\'autre sans ouvrir les Réglages.'),
  H2('Station'),
  B('**Indicatif** et **locator** (4 ou 6 caractères) : nécessaires pour émettre, pour les distances et pour fbbspots.'),
  B('**Indicatifs connus** : une liste séparée par des virgules. Le décodeur fait pour eux une recherche approfondie des trames OPERA 05 trop faibles pour le décodage normal. Chacun peut être suivi de son locator, par exemple `F5ZZO JN12AX, F1HDI` : il sert aux trames reçues sans locator (OPERA 05, TWIST S).'),
  B('**Chercher sur fbbspots le locator des indicatifs reçus sans locator** : une question au serveur spots.f6fbb.org par indicatif, la réponse gardée 30 jours.'),
  P('Pour une trame sans locator, le programme cherche dans l\'ordre : la liste ci-dessus, les trames TWIST L déjà reçues de cet indicatif, puis fbbspots. Le locator trouvé est affiché en minuscules, en italique grisé, dans la liste des spots (`JN12AX` reçu dans la trame, `jn12ax` trouvé ailleurs, comme sur le site fbbalise) (avec la distance et l\'azimut), et sa provenance s\'affiche au survol. Il n\'est ni publié en MQTT ni écrit dans le journal CSV, qui gardent ce que la trame contenait.'),
  H2('Audio'),
  ...fig('settings-audio.png', 470, 'Réglages, onglet Audio'),
  B('**Entrée (réception)** : l\'entrée qui reçoit la sortie audio du récepteur (carte son, interface USB du transceiver, câble virtuel). Au réveil de l\'ordinateur après une mise en veille, ou si elle ne donne plus de son pendant 5 secondes, l\'entrée est rouverte d\'elle-même (le journal le signale) ; le décodage reprend une trentaine de secondes plus tard, le temps de remplir la mémoire audio.'),
  B('**Sortie (émission)** : la sortie reliée à l\'entrée audio de l\'émetteur.'),
  B('**Niveau de sortie** et **ton d\'émission** (Hz) : le niveau se règle de préférence pendant le Tune (chapitre 6).'),
  P('La fréquence d\'échantillonnage est celle du périphérique ; la réception est ramenée à 12 kHz.'),
  H2('Radio'),
  P('Voir le chapitre 7 : contrôle CAT, PTT et fréquence de la radio.'),
  H2('Décodage'),
  ...fig('settings-decoding.png', 470, 'Réglages, onglet Décodage'),
  B('**Protocoles cherchés** : OPERA 05, TWIST S, TWIST L, et **PI4** (aussi dans le menu Réception). Le PI4 n\'est pas coché par défaut.'),
  B('**PI4 : porteuse** et **largeur ±** : la zone du PI4, la sienne (ses quatre tons, espacés de 234 Hz, couvrent environ 700 Hz) ; la porteuse au sens OZ2M, d\'habitude **800 ± 150 Hz**. Le PI4 est émis à la seconde 0 de la minute ; une trame dure 24,3 s.'),
  B('**Centre** et **largeur ±** (Hz) de la zone de recherche, par défaut 1500 ± 200 Hz. Plus la zone est étroite, moins il y a de fausses détections.'),
  B('**Période des passes** : 3 s par défaut.'),
  B('**Étalement** (Hz) : 0 pour une porteuse propre. Pour les signaux étalés en fréquence (diffusion par la pluie, avions en hyperfréquences), l\'énergie de chaque élément est additionnée sur cette largeur.'),
  B('**Retard de la chaîne de réception** (s) : si le signal arrive en retard à la carte son (SDR, SDR par le web, réseau, câble virtuel), les heures de début sont reculées d\'autant. Exemple : une trame partie à 14:14:00 et affichée 14:14:01 → mettre 1. Le retard de la carte son elle-même est déjà compté.'),
  H2('Balise'),
  B('**Durée maximale d\'émission continue** (s) : le chien de garde coupe l\'émission au-delà (120 s par défaut).'),
  B('**Arrêt automatique du Tune** (s) : 30 s par défaut.'),
  P('La séquence, la période et la seconde de départ se règlent dans la fenêtre Balise (chapitre 6).'),
  H2('Rapports'),
  ...fig('settings-reports.png', 470, 'Réglages, onglet Rapports'),
  B('**Journal CSV** des spots : un fichier par jour UTC, `twist-AAAA-MM-JJ.csv`, séparateur point-virgule. Il s\'ouvre dans Excel ou LibreOffice.'),
  B('**Conserver les WAV des passes qui ont décodé** : les 45 s analysées (environ 1 Mo chacune), gardées un nombre de jours et dans une taille totale maximale (les plus anciens partent d\'abord). Utile pendant les essais, et pour comprendre une trame manquée ou douteuse.'),
  B('**Enregistrements** : l\'enregistrement continu (menu Réception) s\'arrête au bout d\'une durée maximale (60 min ; 0 pour sans limite) ou s\'il reste moins d\'un certain espace libre sur le disque du dossier du journal (2 Go).'),
  B('**Envoyer les spots à fbbspots** (spots.f6fbb.org) : avec la **clé de la station**. **Demander une clé…** envoie l\'indicatif et le locator de l\'onglet Station avec votre adresse email ; la clé arrive par email (fbb_ suivi de 40 chiffres) : la coller dans le champ. Une nouvelle demande (même indicatif, même adresse) remplace la clé, par exemple si elle est perdue. Les spots partent par HTTPS, dans l\'ordre ; si le serveur est injoignable, ils attendent et partent à son retour. Le journal du programme dit quand l\'envoi commence, et pourquoi un spot ou la clé est refusé.'),
  ...fig('key-request.png', 380, 'Demander la clé fbbspots de la station'),
  B('**Diagnostic…** (à côté de la clé ; aussi **Aide › Diagnostic fbbspots…**) vérifie, étape par étape, que les spots peuvent partir (chapitre 8). Si le serveur refuse la clé, l\'envoi se met en pause et réessaie toutes les 10 minutes, les spots gardés.'),
  B('**Publier aussi les spots en MQTT** (votre propre serveur, format de spyfbbclient) : serveur, port, TLS, utilisateur, mot de passe et topic, de la forme `fbbspots/<nom de votre station>/spot`. **Tester la connexion** vérifie le serveur et le compte.'),
  P('Sous Windows, la clé et le mot de passe sont enregistrés chiffrés pour votre compte. Seuls les spots reçus en direct sont envoyés, jamais ceux d\'un fichier relu, et avec le locator de la trame seulement.'),
  H2('Interface'),
  B('**Thème** : sombre, clair, ou comme Windows (suit le mode clair ou sombre du système). Il s\'applique à l\'enregistrement.'),
  B('**Langue** : français ou English. Elle s\'applique au **prochain démarrage** du programme : un message propose de **relancer TWIST Console** tout de suite (ou plus tard).'),
  B('**Son à la réception d\'un spot** (activé par défaut) : deux notes brèves sur la sortie son par défaut du système, pour les spots reçus en direct, un toutes les 3 s au plus. **Ne pas l\'activer si cette sortie va à l\'émetteur** : avec le VOX, le son le ferait passer en émission. Le bouton Écouter permet de l\'essayer.'),
  B('**Rechercher les nouvelles versions** (activé par défaut) : au démarrage puis une fois par jour (chapitre 8).'),
  B('**Waterfall** : une **ligne horizontale pointillée** toutes les 1, 2, 5, 10 ou 15 minutes (ou aucune ; 1 minute par défaut), avec l\'heure UTC à gauche ; et, pour une trame en cours, **sa fréquence audio** au lieu de « réception en cours » sur son étiquette (case à cocher, non cochée par défaut).'),
  P('La taille et la position des fenêtres, l\'état des volets et les réglages du waterfall sont mémorisés automatiquement.'),
];

const mainwin = [
  H1('4. La fenêtre principale'),
  ...fig('main-replay.png', 620, 'La fenêtre principale'),
  P('De haut en bas : le menu, la ligne de l\'essentiel, le waterfall, la liste des spots, le journal et la barre d\'état.'),
  H2('La ligne de l\'essentiel'),
  B('**Entrée** : le niveau reçu (le chiffre en dBFS au survol). Il doit rester dans la zone verte, de −40 à −10 dBFS ; au-dessus de −3 dBFS, l\'entrée sature. Le repère marque le maximum récent.'),
  B('**CPU**, juste en dessous et dans le même format : le processeur utilisé par le programme (le pourcentage au survol). Vert jusqu\'à 60 %, orange jusqu\'à 85 %, rouge au-delà : les passes risquent alors d\'être en retard.'),
  B('**Décodage actif** : arrête ou reprend la réception.'),
  B('**Radio** : la fréquence du transceiver (MHz, USB), qui donne la fréquence RF des spots. Lue par CAT si c\'est réglé, sinon à saisir.'),
  B('À droite, **la balise** : son état (un clic ouvre la fenêtre Balise) ; pendant le cycle, **prochaine émission** et son heure UTC ; **Émettre maintenant**, ou **ARRÊT · Échap** pendant une émission.'),
  B('En bout de ligne, **l\'heure UTC**, en grand : l\'heure de l\'ordinateur, qui doit être synchronisée à la seconde près.'),
  P('Le profil, l\'indicatif et le locator sont à droite de la barre de menus. Les protocoles cherchés (OPERA 05, TWIST S, TWIST L, PI4) se cochent dans le menu **Réception** ; la source et la zone de recherche se règlent dans les Réglages ; **Tune** est dans le menu Émission et la fenêtre Balise.'),
  H2('Informations techniques'),
  P('**Affichage › Informations techniques** ajoute une seconde ligne : la source audio, les protocoles cherchés, le centre et la largeur de la zone de recherche, le niveau en dBFS, la **passe** (durée de la dernière analyse et délai de la suivante), le **CPU**, l\'**audio** (fréquence d\'échantillonnage, pertes d\'échantillons) et Tune. Utile pendant les essais ou pour chercher un problème.'),
  ...fig('main-replay-tech.png', 560, 'Avec les informations techniques'),
  H2('Waterfall'),
  P('Le spectre défile vers le bas, la ligne la plus récente en haut, sous l\'échelle des fréquences. Il y a une ligne par point de hauteur : agrandir la fenêtre ou le waterfall montre plus de temps (le texte au-dessus dit combien de secondes), et les lignes déjà là sont gardées. Les pointillés verticaux marquent la zone de recherche ; un pointillé horizontal, avec son heure, le début de chaque minute (réglable : Réglages › Interface). Chaque trame décodée est étiquetée (indicatif et protocole) à l\'endroit où elle s\'est terminée ; une étiquette en pointillés « réception en cours » (ou la fréquence du signal, au choix) apparaît pendant qu\'une trame arrive, à la ligne où le signal a été repéré, et descend avec l\'image.'),
  B('**Gain** et **Contraste** : la luminosité du fond et l\'écart des couleurs.'),
  B('**Bande** : **autour des zones** (1000 Hz autour de la zone de recherche ; avec le PI4, autour des deux zones, celle des trames OPERA / TWIST en pointillés bleus, celle du PI4 en pointillés verts), ou une bande fixe : **200 – 2000 Hz**, ou toute la bande audio (300 – 2700 Hz).'),
  B('**Vitesse** : une ligne toutes les 0,2 s, 0,5 s ou 1 s : de l\'ordre d\'une minute à plusieurs minutes affichées, selon la hauteur.'),
  ...fig('main-receiving.png', 560, 'Un signal en cours de réception : son étiquette sur le waterfall, et l\'en-tête des spots'),
  H2('Spots'),
  P('Une ligne par trame décodée, la plus récente en haut : heure de début (UTC), protocole, indicatif, locator, SNR, fréquence audio, fréquence RF, distance et azimut (si votre locator est réglé). Un locator en italique grisé n\'a pas été reçu dans la trame : il vient de la liste des Réglages, d\'une trame TWIST L déjà reçue ou de fbbspots (chapitre 3, Station). Il est alors en minuscules (`jn12ax`), alors qu\'un locator reçu est en majuscules (`JN12AX`). **Ouvrir le journal CSV** ouvre le fichier du jour ; **Vider la liste** efface l\'affichage seulement (les spots restent dans le journal), et un bouton Annuler permet de la retrouver. Pendant qu\'un signal arrive, « réception en cours », sa fréquence et l\'heure où il a été repéré prennent la place des compteurs dans l\'en-tête, sans décaler la liste.'),
  H2('PI4'),
  P('Une balise PI4 alterne souvent son indicatif et « /locator » d\'une minute à l\'autre (F5ZZO, puis /JN12AX). Le locator reçu est rattaché à l\'indicatif d\'une trame PI4 de la même balise : reçue sur la même fréquence (6 Hz près) et une, deux ou trois minutes rondes avant ou après, puisque le PI4 est toujours émis sur la minute. La ligne montre le locator reçu (en majuscules) et l\'indicatif en minuscules, en italique grisé, comme un locator deviné (`f5zzo`), puisqu\'il n\'a pas été reçu dans ce message ; il est envoyé ainsi à fbbspots et en MQTT, et son survol dit d\'où il vient. Si le locator arrive avant l\'indicatif (au lancement, par exemple), la ligne attend : elle reçoit l\'indicatif quand sa trame arrive, et n\'est envoyée qu\'à ce moment-là ; sans indicatif au bout de trois minutes et demie, elle est seulement écrite dans le journal CSV. Quand le PI4 est cherché, le waterfall montre sa zone en pointillés verts, à côté de celle des trames OPERA / TWIST en pointillés bleus, et la bande affichée s\'élargit pour contenir les deux. Pendant qu\'une trame PI4 arrive, une étiquette verte « réception PI4 en cours » s\'affiche à sa porteuse (les quatre tons sont reconnus ensemble ; le ton le plus haut, qui peut tomber dans la zone OPERA / TWIST, n\'y est pas pris pour une trame).'),
  ...fig('main-pi4.png', 560, 'Deux trames PI4 de F5ZZO : l\'indicatif, puis le locator de la minute suivante'),
  H2('Indicatifs douteux'),
  P('Près du seuil de sensibilité, une trame abîmée peut passer les contrôles et donner un indicatif « fantôme ». Chaque indicatif décodé est donc comparé aux préfixes réellement utilisés par les radioamateurs (fichiers de pays de AD1C), et pour une trame TWIST L, son locator au pays de l\'indicatif (avec une marge de quelques centaines de kilomètres). La distance n\'est pas un critère : en ondes courtes, un spot lointain peut être réel.'),
  B('Un indicatif douteux est affiché **grisé, suivi de « ? »** ; le survol en donne la raison (« préfixe inconnu », « locator OL30XN loin du pays de l\'indicatif »…).'),
  B('Il n\'est **pas envoyé** à fbbspots ni en MQTT, ne déclenche pas le son, et son locator n\'est pas retenu. Il reste dans le journal CSV, la raison dans la colonne `douteux`.'),
  B('**Clic droit › Supprimer la ligne**, ou la touche **Suppr**, retire une ligne de la liste (douteuse ou non) ; elle reste dans le journal CSV.'),
  B('Les indicatifs de la liste des Réglages (Station), et ceux trouvés par la recherche profonde, ne sont jamais jugés douteux.'),
  ...fig('main-doubt.png', 560, 'Un indicatif douteux en haut de la liste : grisé, suivi de « ? »'),
  H2('Journal'),
  P('Les messages du programme : démarrage de la capture, trames décodées, liaison radio, fbbspots, MQTT, erreurs. Le dernier avertissement est rappelé à droite de son titre ; il s\'efface quand la cause a disparu (niveau d\'entrée redevenu correct, par exemple) ou au bout de 15 minutes.'),
  H2('Volets repliables et détachables'),
  P('Le waterfall et le journal se replient en cliquant sur leur titre, ou avec **F2** et **F3** (menu Affichage). Chacune des trois parties — waterfall, spots, journal — peut aussi être **détachée** dans une fenêtre à elle, par exemple sur un second écran : le bouton à deux fenêtres à droite de son titre, ou Affichage › … dans une fenêtre séparée. Fermer cette fenêtre, ou le même bouton, la remet dans la fenêtre principale. Dans la fenêtre principale, la partie restante qui peut grandir prend toute la hauteur. Les positions et tailles sont gardées pour le démarrage suivant. Ces fenêtres sont indépendantes : réduire la fenêtre principale les laisse à l\'écran ; quitter le programme les ferme.'),
  P('Quand le waterfall et les spots sont tous deux dans la fenêtre principale, ils se partagent la hauteur : l\'espace entre les deux se tire à la souris, et la proportion est gardée. Ni l\'un ni l\'autre ne descend sous une hauteur minimale (une image de waterfall lisible, l\'en-tête et quatre lignes de spots).'),
  ...fig('main-spots-detached.png', 520, 'Les spots dans une fenêtre séparée : le waterfall prend la hauteur'),
];

const reception = [
  H1('5. Réception'),
  H2('Brancher le récepteur'),
  P('Relier la sortie audio du récepteur (prise casque, sortie ligne, ou l\'interface USB du transceiver) à une entrée de la carte son, la choisir dans Réglages › Audio (ou **Source** des informations techniques), en mode USB sur la fréquence de la balise. Régler le volume du récepteur pour que le niveau d\'entrée soit dans la zone verte. Le signal de la balise doit tomber dans la zone de recherche : par exemple, pour une balise sur 144,490 MHz et une zone centrée sur 1500 Hz, régler le récepteur sur 144,4885 MHz USB.'),
  H2('Recevoir un SDR par le web'),
  P('Pour décoder le son d\'un SDR écouté dans un navigateur (WebSDR, OpenWebRX…), il faut un câble audio virtuel qui ramène la sortie du navigateur vers une entrée :'),
  B('**Windows** : VB-Audio Virtual Cable (gratuit). Choisir « CABLE Input » comme sortie du navigateur (Paramètres › Son › Volume des applications) et « CABLE Output » comme Source dans TWIST Console.'),
  B('**Mac** : BlackHole 2ch (gratuit, existential.audio/blackhole), puis dans Configuration audio et MIDI : « + » › Créer un périphérique à sorties multiples, cocher Sortie intégrée (en premier) et BlackHole 2ch (correction de dérive sur BlackHole), clic droit › Utiliser ce périphérique pour la sortie audio. Dans TWIST Console, relancé : Source › BlackHole 2ch. Les touches de volume du Mac n\'agissent pas sur une sortie multiple : régler le niveau avec le volume de la page du SDR.'),
  note('Un SDR par le web retarde le son de 1 à 3 secondes : si les heures de début sont en retard, régler Réglages › Décodage › Retard de la chaîne de réception.'),
  H2('Relire un fichier WAV'),
  P('**Fichier › Rejouer un fichier WAV…** (ou Source › Fichier WAV…) relit un enregistrement par la même chaîne de décodage que la carte son. Une bande **Lecture du fichier** montre l\'avancement (barre, temps lu et durée, pourcentage) et le nombre de trames décodées.'),
  ...fig('main-replay-progress.png', 560, 'Relecture d\'un enregistrement, à mi-chemin'),
  B('La lecture se fait en temps réel, pour que le waterfall défile normalement. **Lecture rapide** va aussi vite que le décodage le permet (le waterfall ne suit pas) ; elle se change pendant la lecture.'),
  B('**Arrêter**, **Relire**, et **Revenir à l\'entrée audio** pour reprendre la réception par la carte son.'),
  B('Les heures des spots comptent à partir du début de la relecture. Les spots d\'un fichier ne sont envoyés ni à fbbspots ni en MQTT.'),
  B('Formats acceptés : WAV PCM 8 à 32 bits ou flottant, mono ou stéréo (les voies sont moyennées), à toute fréquence d\'échantillonnage.'),
  P('**Fichier › Dossier des WAV conservés** ouvre le dossier des passes gardées (Réglages › Rapports). Chaque fichier est nommé par l\'heure UTC de son premier échantillon et la fréquence de la radio, par exemple `twist-20261029-101500Z-144.488500MHz.wav`.'),
  H2('Enregistrer l\'audio'),
  P('Pour garder ce qui n\'a pas été décodé (une réflexion sur un avion, un signal à analyser), deux commandes du menu **Réception** :'),
  B('**Garder les 2 dernières minutes** (**Ctrl+S**) : écrit tout de suite les deux minutes qui précèdent, que le programme garde toujours en mémoire. Le fichier est nommé par l\'heure UTC de son premier échantillon, la fréquence de la radio et sa durée (par exemple `twist-20261111-143205Z-432.400000MHz-120s.wav`). C\'est la commande à utiliser quand on voit passer quelque chose sur le waterfall.'),
  B('**Enregistrer en continu** : enregistre à partir de maintenant, jusqu\'à **Arrêter l\'enregistrement continu**, au bout de la durée maximale, ou s\'il ne reste plus assez de place sur le disque (Réglages › Rapports : 60 min et 2 Go par défaut). Un badge rouge **● REC** et la durée s\'affichent à côté de « Décodage actif » ; un clic dessus l\'arrête. Seulement depuis une entrée audio, pas pendant la relecture d\'un fichier. Le fichier est lisible même si le programme s\'arrête brutalement (son en-tête est mis à jour toutes les 5 s).'),
  P('Les fichiers sont au rythme où le programme décode, environ 12 kHz (11 025 Hz pour une carte à 44,1 kHz) : 1,4 Mo par minute, décodés exactement comme en direct. Ils sont rangés par jour dans le sous-dossier **enregistrements** du dossier du journal (**Réception › Dossier des enregistrements**) et ne sont jamais effacés par le programme. Fichier › Rejouer un fichier WAV les relit.'),
  ...fig('main-recording.png', 560, 'Un enregistrement continu en cours'),
];

const tx = [
  H1('6. Émission : la balise'),
  note('Toujours essayer l\'émission sur une **charge fictive** ou une antenne adaptée, et vérifier la puissance et la fréquence avant de laisser un cycle tourner.', 'warn'),
  P('**Émission › Balise…** (ou un clic sur l\'état de la balise, en haut à droite de la fenêtre principale) ouvre la fenêtre de la balise. L\'émission se fait par la méthode audio : le programme produit le signal manipulé sur la sortie son, au ton d\'émission, et l\'émetteur est en USB.'),
  ...fig('balise.png', 380, 'La fenêtre Balise'),
  H2('Régler le niveau avec Tune'),
  P('**Tune** émet une porteuse continue au ton d\'émission, avec le même PTT que la balise, et s\'arrête seule au bout du temps indiqué (30 s par défaut). Pendant le Tune, monter le **niveau de sortie** jusqu\'à ce que l\'ALC de l\'émetteur commence tout juste à agir : le réglage s\'entend tout de suite, et le même niveau sert à la balise.'),
  H2('La séquence'),
  P('La séquence est la suite d\'étapes émises à chaque cycle :'),
  B('**+ Trame** : une trame OPERA 05, TWIST S, TWIST L ou **PI4** de votre indicatif (et locator pour TWIST L) ;'),
  B('**+ Porteuse** : une porteuse continue d\'une durée donnée ;'),
  B('**+ Pause** : un silence d\'une durée donnée, avec ou sans PTT (relâcher le PTT laisse recevoir pendant la pause) ;'),
  B('**+ CW** : votre indicatif et votre locator en Morse, à la vitesse réglée sous la séquence (12 mots/min par défaut).'),
  P('Les flèches changent l\'ordre, la croix supprime une étape. Sous chaque étape, ce qu\'elle émet ; à droite, sa durée, et la durée totale en haut.'),
  H2('Balise PI4'),
  P('Une trame PI4 dure 24,3 s : 146 symboles de 1/6 s, chacun sur l\'un de quatre tons espacés de 234 Hz, sans coupure de phase d\'un ton à l\'autre. Elle est émise à sa propre fréquence, la **fréquence PI4 nominale** réglée sous la séquence (800 Hz par défaut ; au sens OZ2M, 117 Hz au-dessus du ton 0, les tons allant de 683 à 1386 Hz), et non au ton d\'émission des trames OPERA / TWIST. Son message est, au choix, l\'indicatif, « /locator », ou **les deux alternés** d\'un cycle à l\'autre (l\'indicatif sur les cycles pairs, comptés depuis 1970), comme le font les balises : TWIST Console, en réception, rattache le locator à l\'indicatif (chapitre 4, PI4). Le message a 8 caractères au plus (A-Z, 0-9, « / ») : « /JN12AX » en a 7.'),
  P('Une porteuse ou une étape CW peut aussi être émise à la fréquence PI4 (case **fréquence PI4**) : c\'est ce que fait une balise PI4 après sa trame. Le bouton **Séquence PI4 (1 min)** remplace la séquence par la minute d\'une balise PI4, celle de FbBalise : trame PI4 (indicatif et /locator alternés), identification CW et porteuse à la fréquence PI4, jusqu\'à 57,4 s, toutes les minutes à la seconde 0. La minute n\'est pas remplie jusqu\'au bout : le programme prépare l\'émission suivante une seconde à l\'avance.'),
  ...fig('balise-pi4.png', 380, 'La séquence PI4 : trame, CW et porteuse à 800 Hz'),
  H2('Le cycle'),
  B('**Toutes les** N minutes depuis 00:00 UTC, **à la seconde** S : par exemple toutes les 15 minutes à la seconde 0, la séquence part à hh:00:00, hh:15:00, hh:30:00 et hh:45:00. Le départ est calé sur l\'instant où le son sort vraiment de la carte son.'),
  B('**Durée maximale d\'émission continue** : le chien de garde.'),
  B('**Recevoir entre deux émissions** : le décodage reprend entre les émissions ; sinon il reste arrêté pendant le cycle.'),
  B('**Émettre une fois** envoie la séquence tout de suite ; **Démarrer le cycle** lance les émissions à heure fixe. Le haut de la fenêtre principale montre la prochaine émission.'),
  note('**Échap**, ou le bouton rouge **ARRÊT**, arrête immédiatement toute émission (Tune, séquence, cycle) et relâche le PTT. Le PTT est aussi relâché en cas d\'erreur et à la fermeture du programme.', 'warn'),
];

const radio = [
  H1('7. Radio : CAT et PTT'),
  ...fig('settings-radio.png', 470, 'Réglages, onglet Radio'),
  H2('Contrôle CAT (Hamlib)'),
  B('**Aucun** : pas de liaison ; la fréquence se saisit en haut de la fenêtre principale et le PTT est par VOX.'),
  B('**Hamlib (rigctld lancé par TWIST)** : choisir le transceiver en tapant une partie de son nom (par exemple « 991 »), le port série et la vitesse, puis **Tester la liaison**. TWIST Console lance rigctld (fourni) et l\'arrête en quittant.'),
  B('**rigctld déjà lancé (réseau)** : si un autre programme partage déjà la radio par rigctld, ou si la radio est sur un autre ordinateur : adresse et port (4532 en général).'),
  H2('Commande d\'émission (PTT)'),
  B('**VOX** : l\'émetteur passe en émission de lui-même quand il reçoit du son.'),
  B('**CAT** : par la liaison Hamlib. **PTT « données »** : l\'audio est pris par l\'entrée arrière / USB du transceiver ; si le PTT par CAT ne fonctionne pas, essayer sans.'),
  B('**DTR** ou **RTS** : par une ligne d\'un port série (celui du CAT, ou un autre).'),
  B('**Avance du PTT** (s) : le PTT monte ce temps avant le son, pour laisser basculer le relais du transceiver (0,2 s par défaut).'),
  P('**Tester le PTT (2 s)** fait passer en émission deux secondes : sur charge fictive ou antenne adaptée, comme une émission.'),
  H2('Fréquence de la radio'),
  P('**Lue par CAT** (chaque seconde) ou **saisie** en haut de la fenêtre principale. Elle sert à la fréquence RF des spots (fréquence de la radio + fréquence audio en USB), au nom des WAV conservés et au MQTT.'),
];

const tools = [
  H1('8. Outils et aide'),
  H2('Chips d\'une trame'),
  P('**Outils › Chips d\'une trame…** montre la manipulation d\'une trame (TWIST L, TWIST S ou OPERA 05) pour un indicatif et un locator : la suite des éléments (1 = porteuse, 0 = porteuse coupée), sa forme hexadécimale pour la balise FbBalise, sa durée et un aperçu. **Copier** met le texte dans le presse-papiers.'),
  ...fig('tools.png', 470, 'Outils, chips d\'une trame'),
  H2('WAV de test'),
  P('**Outils › WAV de test…** écrit un fichier WAV d\'une trame telle que l\'émetteur l\'envoie (OOK avec rampes de 5 ms, ou PI4), dans un bruit blanc, pour vérifier un récepteur, la rejouer dans TWIST Console (Fichier › Rejouer un fichier WAV…) ou dans un émetteur. On choisit la trame (pour le PI4 : l\'indicatif ou « /locator »), l\'indicatif et le locator, la fréquence audio (pour le PI4, sa fréquence nominale), le SNR, le temps avant et après la trame, et l\'échantillonnage.'),
  P('Le **SNR** est celui que la liste des spots affichera : la puissance du signal moyennée sur la trame, dans 2500 Hz. Une trame OPERA / TWIST, dont la porteuse n\'est présente que sur la moitié environ des éléments, a donc une porteuse 3 dB plus forte que ce chiffre ; pour le PI4, toujours émis, c\'est sa porteuse. Sans bruit, la trame seule est écrite. Dès qu\'il est écrit, le fichier est relu par le décodeur, et le résultat s\'affiche : protocole, indicatif, SNR, fréquence et début trouvés.'),
  ...fig('tools-wav.png', 470, 'Un WAV de test PI4 à −18 dB, relu par le décodeur'),
  H2('Nouvelle version'),
  P('Au démarrage puis une fois par jour, TWIST Console regarde s\'il existe une nouvelle version (sur update.f6fbb.org ; rien n\'est envoyé que la demande elle-même). Si oui, une fenêtre l\'annonce avec ses nouveautés, et un bandeau au-dessus des spots la rappelle (son bouton **Nouveautés** rouvre la fenêtre, **Plus tard** le cache jusqu\'au prochain démarrage). **Installer** (Windows) la télécharge, vérifie le fichier (taille et empreinte SHA-256), puis lance l\'installation : TWIST Console se ferme et se relance tout seul, les réglages gardés. Seule la petite mise à jour est téléchargée quand la version installée le permet. L\'installation est refusée pendant une émission ou un cycle de balise. Sur Mac, **Télécharger** ouvre la page de la version.'),
  ...fig('update-window.png', 420, 'Une nouvelle version annoncée'),
  P('**Aide › Rechercher une mise à jour** le fait tout de suite et répond toujours, par la même fenêtre si une version est disponible, sinon par « Vous avez la dernière version » (ou l\'erreur si le serveur ne répond pas). La recherche automatique se coupe dans Réglages › Interface.'),
  H2('Diagnostic fbbspots'),
  P('Quand les spots n\'arrivent plus sur spots.f6fbb.org, **Aide › Diagnostic fbbspots…** (ou Réglages › Rapports › Diagnostic…) vérifie dans l\'ordre : les réglages (envoi, clé, indicatif) ; ce dont un spot a besoin (une entrée audio en direct, la **fréquence de la radio** : sans elle, aucun spot ne part) ; l\'envoi (spots envoyés, en attente, dernier envoi réussi, dernière erreur) ; le nom du serveur ; la connexion HTTPS (certificat, pare-feu, antivirus) ; l\'**heure du PC** comparée à celle du serveur ; enfin la clé elle-même, et le dernier spot que le serveur a reçu avec elle. Chaque étape est marquée ✔, ⚠ ou ✘ avec ce qu\'elle a trouvé.'),
  P('**Copier** met le résultat dans le presse-papiers, pour l\'envoyer à l\'administrateur ; il est aussi écrit dans le journal, donc dans le rapport pour les testeurs.'),
  ...fig('diag-fbbspots.png', 470, 'Diagnostic fbbspots : ici, la fréquence de la radio inconnue'),
  H2('Rapport pour les testeurs'),
  P('**Aide › Créer un rapport pour les testeurs…** fait un fichier zip avec les versions, les cartes son et l\'état de la radio, les messages du journal, les réglages (sans le mot de passe MQTT), le journal CSV du jour et le dernier WAV conservé. Il suffit de l\'envoyer avec la description du problème. Si une trame a été manquée, joindre aussi le WAV de l\'heure correspondante (Fichier › Dossier des WAV conservés) : seules les passes qui ont décodé sont gardées.'),
  H2('À propos et crédits'),
  P('**Aide › À propos de TWIST Console** donne les versions du programme, du moteur et de Hamlib, et où sont les réglages. Sa partie **Crédits** dit d\'où vient le travail : les auteurs des protocoles et des algorithmes repris (OPERA, PI4, JT4, décodeur de Fano, code LDPC), des bibliothèques (FFTW, PortAudio, Hamlib, Avalonia…) et des données (préfixes, contours des pays). Le détail, avec les licences, est dans le fichier **CREDITS.md** installé à côté du programme.'),
  ...fig('about-credits.png', 360, 'À propos : les crédits'),
  H2('Raccourcis clavier'),
  table([['Touche', 2200], ['Action', 7438]], [
    ['F2', 'Replier / déplier le waterfall (le ramener devant s\'il est détaché)'],
    ['F3', 'Replier / déplier le journal (le ramener devant s\'il est détaché)'],
    ['Échap', 'Arrêt immédiat de toute émission'],
  ]),
];

const trouble = [
  H1('9. En cas de problème'),
  table([['Ce qu\'on voit', 3400], ['Ce qu\'il faut vérifier', 6238]], [
    ['Aucune entrée audio dans Source', 'L\'interface est branchée avant le démarrage du programme ; sur Mac, l\'accès au micro est autorisé. Relancer TWIST Console après avoir branché une carte son ou installé un câble virtuel.'],
    ['Niveau d\'entrée à « — » ou très bas', 'Volume du récepteur, entrée choisie, entrée non muette dans les réglages son du système ; sur Mac, l\'accès au micro.'],
    ['Niveau rouge (saturation)', 'Baisser le volume du récepteur ou de la page du SDR : un signal saturé décode mal.'],
    ['Rien n\'est décodé', 'Le signal est dans la zone de recherche (pointillés du waterfall) ; le protocole est cherché ; le récepteur est en USB. Une trame est décodée quelques secondes après sa fin.'],
    ['Les heures de début sont en retard', 'Horloge de l\'ordinateur synchronisée ; Retard de la chaîne de réception (SDR, SDR par le web, câble virtuel).'],
    ['Fenêtre noire ou vide (Windows)', 'Raccourci TWIST Console (affichage logiciel).'],
    ['Les spots n\'arrivent plus sur fbbspots', 'Aide › Diagnostic fbbspots… : il dit quelle étape bloque (souvent la fréquence de la radio inconnue, l\'heure du PC, ou un pare-feu).'],
    ['Le PTT ne passe pas', 'Tester la liaison, puis Tester le PTT ; essayer sans PTT « données » ; vérifier le port DTR / RTS.'],
    ['Le programme s\'arrête', 'Le fichier twist-error.log (chapitre 2, Où sont les fichiers) donne la raison : l\'envoyer avec le rapport.'],
  ]),
  gap(),
  P('Le guide des testeurs (Aide › Guide des testeurs) liste ce qu\'il est utile d\'essayer dans chaque version.'),
];

// ------------------------------------------------------------------ document

const doc = new Document({
  creator: 'F6FBB',
  title: 'TWIST Console — Guide de l\'utilisateur',
  description: 'Guide de l\'utilisateur de TWIST Console ' + VERSION,
  styles: {
    default: { document: { run: { font: 'Calibri', size: 22 } } },
    paragraphStyles: [
      { id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 34, bold: true, color: ACCENT }, paragraph: { spacing: { before: 240, after: 200 }, outlineLevel: 0 } },
      { id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 26, bold: true, color: '222222' }, paragraph: { spacing: { before: 280, after: 120 }, outlineLevel: 1, keepNext: true } },
      { id: 'Heading3', name: 'Heading 3', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 23, bold: true }, paragraph: { spacing: { before: 200, after: 80 }, outlineLevel: 2, keepNext: true } },
    ],
  },
  numbering: {
    config: [
      { reference: 'bullets', levels: [
        { level: 0, format: LevelFormat.BULLET, text: '•', alignment: AlignmentType.LEFT, style: { paragraph: { indent: { left: 567, hanging: 284 } } } },
        { level: 1, format: LevelFormat.BULLET, text: '–', alignment: AlignmentType.LEFT, style: { paragraph: { indent: { left: 1134, hanging: 284 } } } },
      ] },
      ...numberingConfigs,
    ],
  },
  features: { updateFields: true },
  sections: [{
    properties: { page: { size: { width: 11906, height: 16838 }, margin: { top: 1134, bottom: 1134, left: 1134, right: 1134 } }, titlePage: true },
    headers: { default: new Header({ children: [new Paragraph({ alignment: AlignmentType.RIGHT,
      children: [new TextRun({ text: 'TWIST Console ' + VERSION + ' — Guide de l\'utilisateur', size: 16, color: '888888' })] })] }) },
    footers: { default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER,
      children: [new TextRun({ children: [PageNumber.CURRENT], size: 18, color: '888888' })] })] }) },
    children: [...titlePage, ...toc, ...intro, ...install, ...settings, ...mainwin, ...reception, ...tx, ...radio, ...tools, ...trouble],
  }],
});

Packer.toBuffer(doc).then(b => {
  fs.writeFileSync(path.join(OUT, 'TWIST-Console-Guide-utilisateur.docx'), b);
  console.log('ok', b.length);
});

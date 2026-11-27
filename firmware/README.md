# twist_encoder — encodeur TWIST v1 pour la balise (C autonome)

Deux fichiers à copier dans le projet ESP32 : `twist_encoder.h` et
`twist_encoder.c`. Aucune dépendance (seulement `<stdint.h>` et
`<string.h>`), aucune allocation, environ 2,7 ko de tables constantes
(en flash sur ESP32). Compile en C (C99) et en C++ (Arduino / PlatformIO).

## Utilisation

```c
#include "twist_encoder.h"

static uint8_t chips[TWIST_MAX_CHIPS];
static int nchips;

void twist_setup(void)
{
    nchips = twist_encode("F5ZZO", "JN12ax", chips);   /* trame longue : 321 chips */
    /* nchips = twist_encode("F5ZZO", NULL, chips);       trame courte : 239 chips */
    if (nchips < 0) { /* indicatif ou locator non encodable */ }
}
```

La trame se calcule une fois, au démarrage ou au changement de
configuration. Émission ensuite, exactement comme une trame OPERA Op05 :
pour i = 0 … nchips-1, porteuse **ON si `chips[i]`**, OFF sinon, pendant
**128 ms** par chip (`TWIST_CHIP_MS`). Durée : 30,6 s (courte), 41,1 s
(longue).

- Version compacte : `twist_encode_packed()` remplit 41 octets (8 chips
  par octet), lus avec `TWIST_CHIP(packed, i)`.
- Cadencer les chips sur une horloge (timer matériel, `esp_timer`, ou
  instant de départ + i × 128 ms), pas sur une suite de `delay(128)` qui
  dérive : le décodeur tolère un peu d'écart, mais une trame de 41 s
  doit garder un rythme régulier.
- Pas de synchronisation horaire imposée : la trame peut partir à
  n'importe quelle seconde (comme OPERA). Laisser au moins quelques
  secondes entre deux trames.
- Indicatif : format OPERA (6 caractères au plus, chiffre en 2e ou 3e
  position, pas de « /P »). Locator : 4 ou 6 caractères Maidenhead.

## Vérification

Les chips doivent être identiques à ceux de `twistenc` (fourni avec
twistd). Sur PC :

```
gcc -std=c99 -O2 -o test_encoder test_encoder.c twist_encoder.c
./test_encoder F5ZZO:JN12ax F6FBB
twistenc F5ZZO JN12ax          # même suite de chips
```

Vérifié sur 316 trames (310 indicatifs/locators aléatoires + cas
d'erreur) : identique à `twistenc` dans tous les cas, invalides compris.
Définition complète de la trame : `PROTOCOL.md` de twistd.

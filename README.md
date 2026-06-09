# Réveil ESP32-S3 LVGL

Projet Arduino IDE pour ESP32-S3 avec écran tactile 480x320 en orientation paysage.

## Dépendances

Installez les bibliothèques suivantes dans l'Arduino IDE :

- `lvgl`
- `TFT_eSPI`
- `SD` et `SPI` (fournies avec le cœur ESP32)

Configurez ensuite `TFT_eSPI` pour votre écran ESP32-S3 dans `User_Setup.h` ou via un fichier de configuration équivalent.

## Interface créée

L'interface principale reprend une composition type réveil nocturne :

- Arrière-plan plein écran avec ciel sombre, étoiles, soleil, silhouettes de collines et pastilles décoratives.
- Les informations (heure, date, carte réveil et boutons) sont toutes posées au-dessus de cet arrière-plan.
- Le fond est isolé dans une fonction dédiée côté LVGL (`create_sky_background`) et dans une couche CSS dédiée côté prévisualisation (`.sky-background`) pour pouvoir le remplacer plus tard par un thème, une image ou une animation sans déplacer l'interface.
- Heure principale en grand à gauche.
- Date au format textuel français sous l'heure (`JEUDI 05 JUIN`).
- Carte réveil translucide en haut à droite avec le prochain réveil actif à déclencher, calculé selon le jour courant, l'heure courante, les jours cochés et l'état activé/désactivé.
- Boutons `MENU` et `REVEIL` en bas de l'écran.
- La page `MENU` affiche un bouton `SONNERIE` avec le choix actuel et le nombre de sonneries disponibles ; ce bouton ouvre la liste compacte permettant d'écouter et de choisir une sonnerie. Sa barre de défilement reste masquée dans la prévisualisation HTML comme dans LVGL, tandis que le geste tactile vertical et les flèches haut/bas permettent toujours de parcourir la liste.
- Le bouton `REVEIL` ouvre maintenant une liste dédiée avec plusieurs réveils, interrupteurs d'activation, suppression et bouton `AJOUTER`.
- Le bouton `AJOUTER` ouvre un formulaire `NOUVEAU RÉVEIL` permettant de choisir l'heure, les minutes et les jours cochés avant l'enregistrement.
- À l'heure programmée, un réveil actif lance automatiquement la sonnerie choisie. Dans la prévisualisation, elle joue en boucle jusqu'à l'appui sur `ARRÊTER LA SONNERIE`.

## Prévisualisation PC

Ouvrez `index.html` directement dans un navigateur pour visualiser une version HTML/CSS de l'interface en taille exacte `480x320`.

Pour ajouter des sonneries dans la prévisualisation :

1. déposez les fichiers audio dans `musiques/` ;
2. servez le dépôt avec un petit serveur HTTP (par exemple `python3 -m http.server 8000`) et ouvrez `http://localhost:8000`.

La prévisualisation lit automatiquement les fichiers audio exposés par l’index du dossier. Sur GitHub Pages, où cet index n’est pas disponible, l’action GitHub **Synchroniser la playlist des musiques** régénère et enregistre automatiquement `musiques/playlist.json` après chaque ajout, renommage ou suppression dans `musiques/`.

Le bouton **SYNCHRONISER** recharge immédiatement cette playlist sans utiliser le cache du navigateur. Le bouton **DOSSIER LOCAL** permet aussi de choisir et écouter temporairement les fichiers d’un dossier local, y compris lorsque `index.html` est ouvert directement.

La prévisualisation HTML sépare également l'arrière-plan (`.sky-background`) du contenu (`.content-layer`) afin de faciliter les futures modifications visuelles.

## Sketch Arduino

Le fichier `reveil.ino` contient :

- l'initialisation `TFT_eSPI` et LVGL ;
- un tampon de rendu partiel LVGL ;
- la création de l'écran principal ;
- une fonction dédiée pour le décor de fond ;
- l'affichage dynamique de l'heure, de la date et du prochain réveil actif à déclencher ;
- un écran `REVEILS` listant les alarmes enregistrées ;
- un écran `NOUVEAU REVEIL` pour ajouter une alarme avec choix de l'heure, des minutes et des jours ;
- un écran `MENU` avec un sous-menu `SONNERIE` qui parcourt `/musiques` sur une carte SD, propose les fichiers audio reconnus dans une liste compacte navigable avec des flèches haut/bas et mémorise le choix courant ;
- un point d’intégration `play_music_preview()` appelé pour la préécoute et pour le déclenchement automatique du réveil, à relier au décodeur audio / périphérique I2S propre à la carte ;
- un emplacement `read_touchscreen()` à adapter au contrôleur tactile réel.

## À adapter au matériel

La fonction tactile est volontairement vide pour rester compatible avec différents modules :

```cpp
static bool read_touchscreen(uint16_t *x, uint16_t *y) {
  // Exemple si votre configuration TFT_eSPI expose getTouch() :
  // return tft.getTouch(x, y);
  (void)x;
  (void)y;
  return false;
}
```

## Carte SD et lecture audio

Le sketch utilise le lecteur SD intégré à l’ESP32 avec la broche CS `MUSIC_SD_CS` (valeur par défaut : `10`). Adaptez cette constante à votre câblage et placez les fichiers dans `/musiques` sur la carte SD. Les extensions reconnues sont `.mp3`, `.wav`, `.ogg`, `.m4a`, `.aac` et `.flac`.

La sortie audio dépendant fortement du matériel utilisé (DAC, amplificateur, broches I2S et bibliothèque de décodage), `play_music_preview()` journalise actuellement le morceau demandé. Elle est appelée lors de la préécoute et lorsque l'heure d'un réveil actif est atteinte. Reliez cette fonction à votre pilote audio pour obtenir la lecture sur le réveil physique.

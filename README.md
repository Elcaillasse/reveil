# Réveil ESP32-S3 LVGL

Projet Arduino IDE pour ESP32-S3 avec écran tactile 480x320 en orientation paysage.

## Dépendances

Installez les bibliothèques suivantes dans l'Arduino IDE :

- `lvgl`
- `TFT_eSPI`

Configurez ensuite `TFT_eSPI` pour votre écran ESP32-S3 dans `User_Setup.h` ou via un fichier de configuration équivalent.

## Interface créée

L'interface principale reprend une composition type réveil nocturne :

- Arrière-plan plein écran avec ciel sombre, étoiles, soleil, silhouettes de collines et pastilles décoratives.
- Les informations (heure, date, carte réveil et boutons) sont toutes posées au-dessus de cet arrière-plan.
- Le fond est isolé dans une fonction dédiée côté LVGL (`create_sky_background`) et dans une couche CSS dédiée côté prévisualisation (`.sky-background`) pour pouvoir le remplacer plus tard par un thème, une image ou une animation sans déplacer l'interface.
- Heure principale en grand à gauche.
- Date au format textuel français sous l'heure (`JEUDI 05 JUIN`).
- Carte réveil translucide en haut à droite avec l'heure du prochain réveil actif à se déclencher selon l'heure courante et les jours cochés.
- Boutons `MENU` et `REVEIL` en bas de l'écran.
- Le bouton `REVEIL` ouvre maintenant une liste dédiée avec plusieurs réveils, interrupteurs d'activation, suppression et bouton `AJOUTER`.
- Le bouton `AJOUTER` ouvre un formulaire `NOUVEAU RÉVEIL` permettant de choisir l'heure, les minutes et les jours cochés avant l'enregistrement.

## Prévisualisation PC

Ouvrez `index.html` directement dans un navigateur pour visualiser une version HTML/CSS de l'interface en taille exacte `480x320`.

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
- des callbacks prêts à étendre pour le futur écran `MENU` ;
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

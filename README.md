# Réveil ESP32-S3 LVGL

Projet Arduino IDE pour ESP32-S3 avec écran tactile 480x320 en orientation paysage.

## Dépendances

Installez les bibliothèques suivantes dans l'Arduino IDE :

- `lvgl`
- `TFT_eSPI`

Configurez ensuite `TFT_eSPI` pour votre écran ESP32-S3 dans `User_Setup.h` ou via un fichier de configuration équivalent.

## Interface créée

- Fond général gris clair.
- Zone horloge verte à gauche : `x=0`, `y=0`, `360x90`.
- Zone animation rouge/marron à gauche : `x=0`, `y=90`, `360x230`.
- Colonne droite grise : `x=360`, `y=0`, `120x320`.
- Date `JJ/MM/AAAA` en haut de la colonne droite.
- Bouton cliquable `REVEIL`, heure de réveil `HH:MM`, puis bouton cliquable `MENU` en bas.

## Prévisualisation PC

Ouvrez `index.html` directement dans un navigateur pour visualiser une version HTML/CSS de l’interface en taille exacte `480x320`. Cette prévisualisation reprend les mêmes positions, dimensions et couleurs que le sketch LVGL.

## À adapter selon le matériel

La fonction `read_touchscreen()` est volontairement isolée dans `reveil.ino` pour brancher plus tard le contrôleur tactile réel du module (FT6236, GT911, CST816, XPT2046, etc.).

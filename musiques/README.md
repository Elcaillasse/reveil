# Musiques du réveil

Déposez dans ce dossier les fichiers audio servis avec l'application (`.mp3`, `.wav`, `.ogg`, `.m4a`, `.aac` ou `.flac`). Lorsqu'elle est servie par un serveur qui expose l'index du dossier, la prévisualisation les détecte automatiquement.

Pour un hébergement qui masque le contenu des dossiers, ajoutez aussi leurs noms dans `playlist.json` :

```json
[
  "douce-matinee.mp3",
  "oiseaux.wav"
]
```

La page **MENU** les affichera, permettra de les écouter et de choisir la sonnerie. Dans la prévisualisation ouverte directement depuis le disque, le bouton **OUVRIR DOSSIER** permet aussi de charger temporairement un dossier sans modifier `playlist.json`.

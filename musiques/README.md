# Musiques du réveil

Déposez dans ce dossier les fichiers audio servis avec l'application (`.mp3`, `.wav`, `.ogg`, `.m4a`, `.aac` ou `.flac`). Lorsqu'elle est servie par un serveur qui expose l'index du dossier, la prévisualisation les détecte automatiquement.

À chaque ajout, renommage ou suppression dans ce dossier sur GitHub, l’action **Synchroniser la playlist des musiques** régénère automatiquement `playlist.json`. Il n’est donc plus nécessaire de modifier cette liste à la main. L’action peut également être lancée manuellement depuis l’onglet **Actions** de GitHub.

La page **MENU** les affichera, permettra de les écouter et de choisir la sonnerie. Le bouton **SYNCHRONISER** recharge la liste sans cache. Dans la prévisualisation ouverte directement depuis le disque, le bouton **DOSSIER LOCAL** permet aussi de charger temporairement un dossier sans modifier `playlist.json`.

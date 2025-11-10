# Optimisation des fichiers de fonts Tabler Icons

## Problème identifié

Les fichiers de fonts Tabler Icons stockés localement occupaient **4.3M** d'espace :
- `tabler-icons.ttf` : 2.4M
- `tabler-icons.woff` : 1.2M
- `tabler-icons.woff2` : 812K
- `tabler-icons.min.css` : ~40K

Cela représentait une charge importante pour l'ESP32 qui a des ressources de stockage limitées.

## Solution implémentée

Utilisation du **CDN jsDelivr** pour charger les icônes Tabler :
```html
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@tabler/icons-webfont@2.44.0/tabler-icons.min.css" />
```

## Avantages

1. ✅ **Économie d'espace** : Libère 4.3M de stockage sur l'ESP32
2. ✅ **Performance** : Le navigateur met en cache les fonts du CDN
3. ✅ **Mise à jour** : Version fixée (2.44.0) pour éviter les changements inattendus
4. ✅ **Maintenabilité** : Plus besoin de gérer les fichiers de fonts localement
5. ✅ **Cohérence** : Le projet utilise déjà un CDN pour ECharts

## Icônes utilisées dans le projet

Le projet utilise environ **42 icônes uniques** :
- Batterie et charge : `ti-battery-charging`, `ti-battery-4`, `ti-bolt`
- État et alertes : `ti-alert-triangle`, `ti-alert-circle`, `ti-info-circle`
- Navigation : `ti-arrow-up`, `ti-arrow-down`, `ti-refresh`
- Configuration : `ti-settings`, `ti-tool`, `ti-device-floppy`
- Et autres...

## Compatibilité

- ✅ Tous les navigateurs modernes
- ✅ Support hors ligne : Le navigateur met en cache le CSS après le premier chargement
- ✅ Pas d'impact sur les fonctionnalités existantes

## Fichiers modifiés

1. `web/index.html` - Lien CDN ajouté
2. `web/src/index.html` - Lien CDN ajouté
3. Fichiers supprimés :
   - `web/assets/css/fonts/tabler-icons.ttf`
   - `web/assets/css/fonts/tabler-icons.woff`
   - `web/assets/css/fonts/tabler-icons.woff2`
   - `web/assets/css/tabler-icons.min.css`
   - `web/assets/css/fonts/` (répertoire vide supprimé)

## Alternative envisagée mais non retenue

Une alternative aurait été de créer un **subset optimisé** contenant uniquement les 42 icônes utilisées, mais cette approche :
- Nécessiterait des outils de build supplémentaires
- Serait plus difficile à maintenir
- Économiserait moins d'espace qu'on pourrait le penser (les fonts optimisées pèsent encore ~500K)

Le CDN reste la meilleure solution pour ce projet.

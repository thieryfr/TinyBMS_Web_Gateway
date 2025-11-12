# Interface locale TinyBMS pour Mac mini

Cette application Node.js fournit une interface web locale exécutée sur le Mac mini. Elle communique directement avec le TinyBMS via un câble USB ↔ UART pour lire et écrire la configuration des registres.

## ✨ Fonctionnalités

- Découverte et sélection du port série TinyBMS
- Lecture complète des registres de configuration TinyBMS (via `/api/registers`)
- Écriture des registres individuels (`POST /api/registers`)
- Redémarrage du TinyBMS (`POST /api/system/restart`)
- Interface web réutilisant le module de configuration existant (`tinybms-config.js`)

## 🔌 Pré-requis

- macOS avec Node.js ≥ 18 installé (`brew install node`)
- Câble USB-UART relié au TinyBMS (3.3V TTL)
- Droits d'accès au périphérique série (généralement `/dev/tty.usbserial-*` ou `/dev/cu.usbserial-*`)

## 🚀 Installation

```bash
cd mac-local
npm install
```

## ▶️ Démarrage du serveur local

```bash
npm start
```

Par défaut, le serveur écoute sur `http://localhost:5173`.

## 🖥️ Utilisation

1. Brancher le TinyBMS au Mac via le câble USB-UART.
2. Ouvrir `http://localhost:5173` dans le navigateur du Mac mini.
3. Sélectionner le port série détecté puis cliquer sur **Se connecter**.
4. La page charge automatiquement les registres TinyBMS et permet de modifier la configuration via les formulaires existants.

## ⚙️ Configuration

Les paramètres par défaut (baudrate 115200 bauds) conviennent au TinyBMS. Ils peuvent être ajustés dans `src/server.js` si nécessaire.

## 📁 Structure

- `src/registers.js` : parse les métadonnées des registres TinyBMS depuis le firmware.
- `src/serial.js` : gère la communication USB-UART (construction/parsing des trames TinyBMS).
- `src/server.js` : serveur Express + API REST.
- `public/` : interface web (HTML/CSS/JS) hébergée par Express.

## 🔒 Remarques

- L'upload OTA n'est pas supporté dans cette version (renvoie HTTP 501).
- Assurez-vous qu'aucun autre service n'utilise le port série pendant la configuration.
- Le serveur doit être relancé si le périphérique USB est débranché/rebranché.

## 🧪 Tests

Les tests automatisés ne sont pas fournis pour ce module. Vérifiez la communication en suivant les logs dans le terminal (`npm start`).

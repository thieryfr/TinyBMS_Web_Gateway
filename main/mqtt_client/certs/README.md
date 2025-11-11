# Certificats MQTTS (MQTT over TLS)

Ce répertoire contient les certificats TLS/SSL pour les connexions MQTT sécurisées.

## 📋 Vue d'ensemble

MQTTS (MQTT over TLS) fournit :
- ✅ **Chiffrement** de toutes les communications MQTT
- ✅ **Authentification** du broker MQTT (vérification certificat serveur)
- ✅ **Authentification mutuelle** optionnelle (certificats client)
- ✅ **Protection** contre man-in-the-middle (MITM)

## 🔑 Types de certificats

### 1. Certificat CA (Certificate Authority)

**Fichier** : `mqtt_ca_cert.pem`

**Usage** : Vérifier l'authenticité du broker MQTT

**Requis** : OUI (si `CONFIG_TINYBMS_MQTT_TLS_VERIFY_SERVER=1`)

**Obtention** :
```bash
# Option 1: Certificat racine du broker (production)
# Demander au fournisseur du broker MQTT

# Option 2: Auto-signé (développement uniquement)
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout mqtt_ca_key.pem \
  -out mqtt_ca_cert.pem \
  -days 3650 \
  -subj "/CN=TinyBMS MQTT CA"
```

### 2. Certificat client (optionnel)

**Fichiers** : `mqtt_client_cert.pem`, `mqtt_client_key.pem`

**Usage** : Authentification mutuelle TLS (mTLS)

**Requis** : NON (sauf si broker exige mTLS)

**Génération** :
```bash
# 1. Générer clé privée client
openssl genrsa -out mqtt_client_key.pem 2048

# 2. Créer requête de signature (CSR)
openssl req -new \
  -key mqtt_client_key.pem \
  -out mqtt_client.csr \
  -subj "/CN=TinyBMS-GW-Device-001"

# 3. Signer avec CA (auto-signé pour dev)
openssl x509 -req \
  -in mqtt_client.csr \
  -CA mqtt_ca_cert.pem \
  -CAkey mqtt_ca_key.pem \
  -CAcreateserial \
  -out mqtt_client_cert.pem \
  -days 365

# 4. Nettoyer
rm mqtt_client.csr
```

## 🚀 Installation

### Étape 1 : Placer les certificats

```bash
# Copier les certificats dans ce répertoire
cp /path/to/mqtt_ca_cert.pem main/mqtt_client/certs/
cp /path/to/mqtt_client_cert.pem main/mqtt_client/certs/  # Si mTLS
cp /path/to/mqtt_client_key.pem main/mqtt_client/certs/   # Si mTLS
```

### Étape 2 : Configuration CMake

Les certificats sont embarqués automatiquement si présents :

```cmake
# main/mqtt_client/CMakeLists.txt
if(CONFIG_TINYBMS_MQTT_TLS_ENABLED)
  target_add_binary_data(mqtt_client.elf
    "certs/mqtt_ca_cert.pem"
    TEXT)
endif()
```

### Étape 3 : Activer MQTTS

```bash
idf.py menuconfig
```

Naviguer vers :
```
Component config → TinyBMS-GW → MQTT Configuration
  [*] Enable MQTTS (MQTT over TLS)
  [*] Verify server certificate
  [ ] Enable client certificate authentication
```

### Étape 4 : Compiler et flasher

```bash
idf.py build flash
```

## ⚙️ Configuration

### Vérification serveur uniquement (recommandé)

```c
CONFIG_TINYBMS_MQTT_TLS_ENABLED=1
CONFIG_TINYBMS_MQTT_TLS_VERIFY_SERVER=1
CONFIG_TINYBMS_MQTT_TLS_CLIENT_CERT_ENABLED=0
```

**Certificats requis** :
- ✅ `mqtt_ca_cert.pem`

**URI broker** : `mqtts://broker.example.com:8883`

### Authentification mutuelle (mTLS)

```c
CONFIG_TINYBMS_MQTT_TLS_ENABLED=1
CONFIG_TINYBMS_MQTT_TLS_VERIFY_SERVER=1
CONFIG_TINYBMS_MQTT_TLS_CLIENT_CERT_ENABLED=1
```

**Certificats requis** :
- ✅ `mqtt_ca_cert.pem`
- ✅ `mqtt_client_cert.pem`
- ✅ `mqtt_client_key.pem`

**URI broker** : `mqtts://broker.example.com:8883`

## 🔒 Sécurité

### ⚠️ Bonnes pratiques

1. **JAMAIS** commiter les clés privées dans Git
   ```bash
   # Ajouter à .gitignore
   echo "main/mqtt_client/certs/*.pem" >> .gitignore
   echo "main/mqtt_client/certs/*.key" >> .gitignore
   ```

2. **Permissions** restrictives sur les clés
   ```bash
   chmod 600 mqtt_client_key.pem
   chmod 644 mqtt_ca_cert.pem
   ```

3. **Rotation** régulière des certificats
   - Certificats client : tous les 90 jours minimum
   - Certificats CA : tous les 2-5 ans

4. **Production** : Utiliser CA publique (Let's Encrypt, DigiCert, etc.)

### 🚨 Certificats auto-signés

**Usage** : Développement/test UNIQUEMENT

**Risques** :
- ❌ Pas de protection contre MITM si CA compromise
- ❌ Difficile à révoquer
- ❌ Pas de validation tiers

**En production** : Utiliser TOUJOURS un CA reconnu

## 🧪 Test de connexion

### Vérifier certificat serveur

```bash
openssl s_client -connect broker.example.com:8883 \
  -CAfile mqtt_ca_cert.pem \
  -showcerts
```

**Attendu** :
```
Verify return code: 0 (ok)
```

### Test avec client certificate

```bash
openssl s_client -connect broker.example.com:8883 \
  -CAfile mqtt_ca_cert.pem \
  -cert mqtt_client_cert.pem \
  -key mqtt_client_key.pem
```

### Test avec mosquitto

```bash
# Vérification serveur seulement
mosquitto_sub -h broker.example.com -p 8883 \
  --cafile mqtt_ca_cert.pem \
  -t "test/topic" -v

# Authentification mutuelle
mosquitto_sub -h broker.example.com -p 8883 \
  --cafile mqtt_ca_cert.pem \
  --cert mqtt_client_cert.pem \
  --key mqtt_client_key.pem \
  -t "test/topic" -v
```

## 📊 Brokers MQTT supportés

| Broker | TLS Support | mTLS Support | Notes |
|--------|-------------|--------------|-------|
| Mosquitto | ✅ | ✅ | Open source, facile à configurer |
| HiveMQ | ✅ | ✅ | Cloud et self-hosted |
| AWS IoT Core | ✅ | ✅ Requis | mTLS obligatoire |
| Azure IoT Hub | ✅ | ✅ | Support x509 |
| Google Cloud IoT | ✅ | ✅ | Support JWT aussi |
| EMQX | ✅ | ✅ | Open source, haute performance |

## 🔧 Dépannage

### Erreur : "certificate verify failed"

**Cause** : CA certificate incorrect ou expiré

**Solution** :
```bash
# Vérifier validité
openssl x509 -in mqtt_ca_cert.pem -text -noout

# Vérifier chaîne
openssl verify -CAfile mqtt_ca_cert.pem mqtt_client_cert.pem
```

### Erreur : "unable to get local issuer certificate"

**Cause** : CA certificate manquant ou incomplet

**Solution** : Inclure la chaîne complète dans `mqtt_ca_cert.pem`

### Erreur : "tlsv1 alert unknown ca"

**Cause** : Broker ne reconnaît pas le client certificate

**Solution** : S'assurer que le broker a le CA qui a signé le client cert

## 📚 Références

- [MQTT Security](https://mqtt.org/mqtt-security/)
- [Mosquitto TLS](https://mosquitto.org/man/mosquitto-tls-7.html)
- [ESP-IDF MQTT TLS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html#_CPPv418esp_mqtt_client_config)
- [OpenSSL Certificate Creation](https://www.openssl.org/docs/man1.1.1/man1/req.html)

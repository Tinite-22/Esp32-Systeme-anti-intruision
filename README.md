# Alarme Connectée ESP32 Multicœur (FreeRTOS & Telegram)

![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue?style=for-the-badge&logo=espressif)
![Framework: FreeRTOS](https://img.shields.io/badge/Framework-FreeRTOS-violet?style=for-the-badge)
![API: Telegram](https://img.shields.io/badge/API-Telegram-2CA5E0?style=for-the-badge&logo=telegram)
![Language: C++](https://img.shields.io/badge/Language-C++-green?style=for-the-badge&logo=c%2B%2B)

Un système de sécurité domotique haute performance et ultra-réactif basé sur un microcontrôleur ESP32. Ce projet utilise un capteur infrarouge (IR/PIR) pour la détection d'intrusion et l'API Telegram pour le contrôle à distance. 

L'architecture logicielle repose sur **FreeRTOS**, exploitant les deux cœurs de l'ESP32 pour séparer la détection matérielle (temps réel) des requêtes réseau (sujettes à la latence), garantissant ainsi une fiabilité totale sans aucun gel du système.

---

## Fonctionnalités

- **Multicœur (FreeRTOS) :** La détection matérielle (Cœur 0) et la communication Wi-Fi/Telegram (Cœur 1) fonctionnent en parallèle.
- **Contrôle par Bot Telegram :** Armement, désarmement et vérification du statut du système directement depuis l'application Telegram.
- **Alertes Instantanées :** Notification push envoyée sur votre smartphone dès qu'une intrusion est détectée.
- **Accès Sécurisé :** Filtrage strict : seul votre compte Telegram (via un ID unique) est autorisé à interagir avec le système.
- **Délai d'Armement :** Temporisation de 15 secondes pour vous laisser le temps de quitter la pièce avant l'activation.

---

## Matériel Requis & Câblage

| Composant | Broche ESP32 (GPIO) | Description |
| :--- | :--- | :--- |
| **Capteur Infrarouge (IR/PIR)** | GPIO 27 | Détecte le mouvement (Passe à l'état LOW en cas de détection) |
| **Buzzer Actif** | GPIO 26 | Sirène d'alarme (clignotement sonore asynchrone) |

---

## Configuration du Bot Telegram

Pour que l'ESP32 puisse communiquer avec vous, vous devez créer un Bot Telegram et récupérer deux informations cruciales : le **Token** du bot et votre **Chat ID** personnel.

### Étape 1 : Créer le Bot (Obtenir le Token)
1. Ouvrez Telegram et cherchez le compte officiel **@BotFather**.
2. Lancez la conversation (`/start`) puis envoyez la commande `/newbot`.
3. Suivez les instructions : donnez-lui un nom, puis un nom d'utilisateur finissant obligatoirement par `bot` (ex: `MonAlarmeMaison_bot`).
4. BotFather va générer un **Token d'accès API** (ex: `123456789:ABCdefGhIJKlmNoPQRsTUVwxyZ`).
5. **Copiez ce Token** et placez-le dans le code à la ligne `TOKEN_BOT`.

### Étape 2 : Sécuriser l'accès (Obtenir votre Chat ID)
1. Cherchez le bot **@userinfobot** ou **@RawDataBot** sur Telegram et envoyez `/start`.
2. Le bot vous répondra avec vos informations. Cherchez la ligne contenant votre ID (ex: `Id: 123456789`).
3. **Copiez ce numéro** et placez-le dans le code à la ligne `ID_CHAT_AUTORISE`. *Ceci empêchera toute personne non autorisée de désactiver votre alarme.*
4. **Important :** Cherchez ensuite votre propre bot (créé à l'étape 1) et envoyez-lui `/start` pour initialiser la conversation.

---

## Installation & Déploiement

1. Clonez ce dépôt.
2. Ouvrez le projet dans **Arduino IDE** ou **PlatformIO**.
3. Installez les bibliothèques suivantes via le gestionnaire de bibliothèques :
   - `UniversalTelegramBot` (par Brian Lough)
   - `ArduinoJson` (par Benoit Blanchon)
4. Modifiez la section de configuration au début du code :
   ```cpp
   const char* ssid = "VOTRE_WIFI";
   const char* mot_de_passe = "VOTRE_MDP";
   const String TOKEN_BOT = "VOTRE_TOKEN_BOT_ICI";
   const String ID_CHAT_AUTORISE = "VOTRE_ID_CHAT_ICI";

5. Téléversez sur votre ESP32 (Vitesse du moniteur série : `115200` bauds).

---

## Architecture du Code (Explications Techniques)

Ce projet utilise des concepts avancés de programmation embarquée. Voici les clés pour comprendre le code source :

### 1. Assignation des Tâches aux Cœurs (`xTaskCreatePinnedToCore`)

```cpp
xTaskCreatePinnedToCore(tacheDetection, "Detection", 4096, NULL, 2, NULL, 0); // Cœur 0
xTaskCreatePinnedToCore(tacheTelegram, "Telegram", 8192, NULL, 1, NULL, 1);   // Cœur 1

```

L'ESP32 possède deux processeurs (Cœurs 0 et 1). Nous assignons la lecture du capteur matériel au **Cœur 0** (avec une priorité haute de 2) et la lourde communication réseau Telegram au **Cœur 1**. Ainsi, même si le réseau Wi-Fi est lent ou que l'API Telegram met du temps à répondre, la détection d'intrusion et la sirène restent instantanées.

### 2. Protection des Données Partagées (Mutex / Section Critique)

```cpp
portENTER_CRITICAL(&mux);
alarmeActive = true;
portEXIT_CRITICAL(&mux);

```

Les variables globales comme `alarmeActive` ou `systemeArme` sont lues et modifiées par les *deux* cœurs en même temps. Pour éviter un conflit mémoire (Data Race) qui ferait planter le système, ces lignes "verrouillent" la variable. Pendant qu'un cœur écrit dedans, l'autre doit attendre une fraction de seconde.

### 3. L'Anti-rebond Matériel (Debounce Logiciel)

```cpp
if ((maintenant - tempsAntiRebond) > DELAI_ANTI_REBOND_MS) { ... }

```

Les capteurs électroniques génèrent souvent du bruit (des micro-oscillations) lorsqu'ils changent d'état. Ce code impose un délai de filtrage de 50 millisecondes pour vérifier que le signal du capteur est stable avant de déclarer une intrusion.

### 4. La Boucle Principale

```cpp
void loop() { vTaskDelay(1000); }

```

Contrairement aux scripts Arduino classiques, la fonction `loop()` est ici quasiment vide. C'est le système d'exploitation temps réel (FreeRTOS) qui prend le relais en gérant les boucles infinies `while(true)` que nous avons créées dans `tacheDetection` et `tacheTelegram`.

---

## Commandes Telegram Disponibles

Une fois le système en ligne (vous recevrez le message `"🔌 Système en ligne."`), envoyez ces messages exactement tels quels à votre bot :

* `Statut` : Renvoie l'état actuel du système (Armé, En veille, ou Alarme active).
* `Activer l'alarme` : Lance un compte à rebours de 15 secondes avant d'armer le système.
* `Désactiver l'alarme` : Coupe la sirène immédiatement et repasse le système en mode veille.

---

## 📄 Licence

Ce projet est mis à disposition sous licence MIT. Sentez-vous libre de le cloner, de le modifier et de l'améliorer !

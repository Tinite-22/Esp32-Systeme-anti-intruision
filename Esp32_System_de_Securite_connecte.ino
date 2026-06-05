#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// --- Configuration ---
const char* ssid = "VOTRE_SSID";
const char* mot_de_passe = "VOTRE_MOT_DE_PASSE";
const String TOKEN_BOT = "VOTRE_TOKEN_BOT";
const String ID_CHAT_AUTORISE = "VOTRE_ID_CHAT";

// --- Broches Matérielles ---
const int brocheIR = 27;
const int brocheBuzzer = 26;

// --- État Partagé (Multicœur) ---
volatile bool systemeArme = true;
volatile bool alarmeActive = false;
volatile bool armementEnAttente = false;
volatile unsigned long heureArmementPrevue = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

WiFiClientSecure clientSecurise;
UniversalTelegramBot bot(TOKEN_BOT, clientSecurise);

void gererNouveauxMessages(int nbNouveauxMessages) {
  for (int i = 0; i < nbNouveauxMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String texte = bot.messages[i].text;

    // Vérification de sécurité pour ignorer les messages provenant d'inconnus
    if (chat_id != ID_CHAT_AUTORISE) {
      bot.sendMessage(chat_id, "⛔ Accès refusé.", "");
      continue;
    }

    if (texte == "Statut") {
      String statut = "Statut du Système :\n";
      statut += systemeArme ? "🛡️ ARMÉ\n" : "💤 MODE VEILLE\n";
      statut += alarmeActive ? "🚨 ALARME ACTIVE !" : "✅ SÉCURISÉ";
      bot.sendMessage(chat_id, statut, "");
    } 
    else if (texte == "Désactiver l'alarme") {
      portENTER_CRITICAL(&mux);
      alarmeActive = false;
      systemeArme = false;
      portEXIT_CRITICAL(&mux);
      digitalWrite(brocheBuzzer, LOW);
      bot.sendMessage(chat_id, "🔇 Alarme désactivée. Système en mode veille.", "");
    } 
    else if (texte == "Activer l'alarme") {
      bot.sendMessage(chat_id, "🛡️ Activation dans 15s... Veuillez quitter la pièce.", "");
      heureArmementPrevue = millis() + 15000;
      armementEnAttente = true;
    }
  }
}

// --- Cœur 0 : Détection en Temps Réel et Buzzer ---
void tacheDetection(void* param) {
  unsigned long dernierBip = 0;
  bool etatBuzzer = false;
  bool dernierEtatIR = HIGH;
  unsigned long tempsAntiRebond = 0;
  const int DELAI_ANTI_REBOND_MS = 50;

  while (true) {
    bool lectureIR = digitalRead(brocheIR);
    unsigned long maintenant = millis();

    // Gestion de l'anti-rebond du capteur infrarouge
    if (lectureIR != dernierEtatIR) tempsAntiRebond = maintenant;

    if ((maintenant - tempsAntiRebond) > DELAI_ANTI_REBOND_MS) {
      if (lectureIR == LOW) { // LOW signifie généralement qu'un mouvement est détecté
        portENTER_CRITICAL(&mux);
        if (systemeArme && !alarmeActive) {
          alarmeActive = true;
          Serial.println(">>> INTRUSION DÉTECTÉE <<<");
        }
        portEXIT_CRITICAL(&mux);
      }
    }
    dernierEtatIR = lectureIR;

    // Gestion du délai d'armement de 15 secondes
    if (armementEnAttente && maintenant >= heureArmementPrevue) {
      portENTER_CRITICAL(&mux);
      systemeArme = true;
      alarmeActive = false;
      armementEnAttente = false;
      portEXIT_CRITICAL(&mux);
      Serial.println("Système ARMÉ.");
    }

    // Gestion du clignotement/sonnerie du buzzer en cas d'alerte
    if (alarmeActive) {
      if (maintenant - dernierBip > 300) {
        etatBuzzer = !etatBuzzer;
        digitalWrite(brocheBuzzer, etatBuzzer ? HIGH : LOW);
        dernierBip = maintenant;
      }
    } else {
      digitalWrite(brocheBuzzer, LOW);
    }

    vTaskDelay(1); // Laisse le processeur respirer
  }
}

// --- Cœur 1 : Connectivité et Notifications ---
void tacheTelegram(void* param) {
  unsigned long derniereVerification = 0;
  bool alerteEnvoyee = false;

  while (true) {
    unsigned long maintenant = millis();

    // Envoi d'une notification push si l'alarme se déclenche
    if (alarmeActive && !alerteEnvoyee) {
      bot.sendMessage(ID_CHAT_AUTORISE, "🚨 Alerte : Intrusion détectée ! 🚨", "");
      alerteEnvoyee = true;
    }
    
    // Réinitialisation de l'état d'envoi si l'alarme est désactivée
    if (!alarmeActive) alerteEnvoyee = false;

    // Vérification des nouveaux messages Telegram toutes les 2 secondes
    if (maintenant - derniereVerification > 2000) {
      int nbNouveauxMessages = bot.getUpdates(bot.last_message_received + 1);
      while (nbNouveauxMessages) {
        gererNouveauxMessages(nbNouveauxMessages);
        nbNouveauxMessages = bot.getUpdates(bot.last_message_received + 1);
      }
      derniereVerification = maintenant;
    }
    vTaskDelay(10);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(brocheIR, INPUT);
  pinMode(brocheBuzzer, OUTPUT);
  digitalWrite(brocheBuzzer, LOW);

  // Connexion au réseau Wi-Fi
  WiFi.begin(ssid, mot_de_passe);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  // Configuration requise pour l'API Telegram
  clientSecurise.setInsecure();
  bot.sendMessage(ID_CHAT_AUTORISE, "🔌 Système en ligne.", "");

  // Assignation des tâches aux deux cœurs du processeur ESP32
  xTaskCreatePinnedToCore(tacheDetection, "Detection", 4096, NULL, 2, NULL, 0); // Exécuté sur le Cœur 0
  xTaskCreatePinnedToCore(tacheTelegram, "Telegram", 8192, NULL, 1, NULL, 1);   // Exécuté sur le Cœur 1
}

void loop() {
  // Le loop principal reste vide, FreeRTOS s'occupe de gérer les tâches
  vTaskDelay(1000);
}
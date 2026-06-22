from flask import Flask, request, jsonify, Response
import whisper
import tempfile
import os
import wave
import re
import json
import struct

app = Flask(__name__)

print("Chargement modèle Whisper medium...")
model = whisper.load_model("medium")
print("Modèle prêt !")

# ================= STOCKAGE GLOBAL =================
last_text   = ""
last_action = None
last_heure  = None

# ================= SEPARATEUR CONSOLE =================
SEP  = "=" * 50
SEP2 = "-" * 50

ACTION_LABELS = {
    "set_alarm":        "REGLER ALARME",
    "deactivate_alarm": "DESACTIVER ALARME",
    
    None:               "aucune action",
}

# ================= ANALYSE COMMANDE =================
def analyser_commande(texte):
    t = texte.lower()
    action = None
    heure  = None

    mots_desactiver = [
        "désactiv", "desactiv",
        "disactiv","Désactivez l'alarme",
        "éteins l'alarme", "eteins l'alarme",
        "stop alarme", "arrête l'alarme", "arrete l'alarme",
        "coupe l'alarme", "supprime l'alarme",
        "annule l'alarme",
    ]

    

    mots_regler = [
        "réveil", "réveill", "reveil",
        "alarm",
        "mettre l'alarme", "mettre une alarme",
        "programme", "règle", "regle",
        "fixe", "mets l'alarme","activ", "allume",
        "démarre l'alarme", "demarre l'alarme",
        "lance l'alarme",
    ]



    # Ordre important : désactiver TOUJOURS avant activer
    if any(m in t for m in mots_desactiver):
        action = "deactivate_alarm"

   
    elif any(m in t for m in mots_regler):
        action = "set_alarm"

    
    # --- Heure : tous les formats ---
    patterns = [
        r'(\d{1,2})\s*h\s*(\d{2})',
        r'(\d{1,2})\s*h\b',
        r'(\d{1,2}):(\d{2})',
        r'(\d{1,2})\.(\d{2})\s*h?',
        r'(\d{1,2})\s*heures?\s*(\d{2})',
        r'(\d{1,2})\s*heures?\b',
    ]

    for pat in patterns:
        match = re.search(pat, t)
        if match:
            h = int(match.group(1))
            m = int(match.group(2)) if len(match.groups()) >= 2 and match.group(2) else 0
            if 0 <= h <= 23 and 0 <= m <= 59:
                heure = f"{h:02d}:{m:02d}"
                if action is None:
                    action = "set_alarm"
                break

    return action, heure


# ================= VOLUME RMS =================
def audio_rms(path):
    try:
        with wave.open(path, "rb") as wf:
            frames    = wf.readframes(wf.getnframes())
            sampwidth = wf.getsampwidth()
            nframes   = wf.getnframes()
            if nframes == 0 or sampwidth != 2:
                return 0
            samples = struct.unpack(f"<{nframes}h", frames[:nframes * 2])
            rms = (sum(s * s for s in samples) / nframes) ** 0.5
            return rms
    except Exception:
        return 0


# ================= BARRE VOLUME =================
def build_bar(rms, max_rms=8000, length=20):
    filled = int(min(rms / max_rms, 1.0) * length)
    empty  = length - filled
    level  = "FORT  " if rms > 5000 else "MOYEN " if rms > 1500 else "FAIBLE"
    return f"[{'#' * filled}{'.' * empty}] {level}  RMS={rms:.0f}"


# ================= ROUTE UPLOAD (ESP audio) =================
@app.route("/upload", methods=["POST"])
def upload():
    global last_text, last_action, last_heure

    audio = request.data

    if len(audio) < 1000:
        print(f"\n[UPLOAD] Audio trop court ({len(audio)} bytes), ignoré.")
        return "", 400

    with tempfile.NamedTemporaryFile(delete=False, suffix=".wav") as tmp:
        tmp.write(audio)
        path = tmp.name

    # --- Validation WAV ---
    try:
        with wave.open(path, "rb") as wf:
            nframes   = wf.getnframes()
            framerate = wf.getframerate()
            nchannels = wf.getnchannels()
            sampwidth = wf.getsampwidth()
            duration_s = nframes / framerate if framerate > 0 else 0

        if nframes == 0:
            os.remove(path)
            print("[UPLOAD] Fichier WAV vide, ignoré.")
            return "", 400

    except Exception as e:
        os.remove(path)
        print(f"[UPLOAD] WAV invalide : {e}")
        return "", 400

    # --- Volume ---
    rms        = audio_rms(path)
    volume_bar = build_bar(rms)

    # --- Transcription Whisper ---
    result = model.transcribe(
        path,
        language="fr",
        temperature=0,
        no_speech_threshold=0.65,
        condition_on_previous_text=False
    )
    texte = result["text"].strip()
    os.remove(path)

    # --- Filtrage bruit ---
    mots_bruit = {"...", "…", "[musique]", "[bruit]", "[silence]",
                  "sous-titres", "merci", ".", "!"}
    if texte == "" or texte in mots_bruit or len(texte) < 3:
        print(f"\n[UPLOAD] Bruit ignoré  →  '{texte}'")
        return ""

    action, heure = analyser_commande(texte)

    last_text   = texte
    last_action = action
    last_heure  = heure

    # ===== AFFICHAGE CONSOLE =====
    action_label = ACTION_LABELS.get(action, str(action))

    print(f"\n{SEP}")
    print(f"  AUDIO RECU")
    print(f"{SEP2}")
    print(f"  Duree  : {duration_s:.2f}s  |  {framerate}Hz  |  {nchannels}ch  |  {sampwidth*8}bit")
    print(f"  Volume : {volume_bar}")
    print(f"{SEP2}")
    print(f"  Texte  : \"{texte}\"")
    print(f"{SEP2}")
    print(f"  Action : {action_label}")
    print(f"  Heure  : {heure if heure else '---'}")
    print(f"{SEP}")

    return Response(
        json.dumps({"texte": texte, "action": action, "heure": last_heure},
                   ensure_ascii=False),
        mimetype="application/json",
        headers={"Connection": "close"}
    )


# ================= ROUTE COMMAND (ESP RTC) =================
@app.route("/command", methods=["GET"])
def command():
    return jsonify({
        "texte":  last_text,
        "action": last_action,
        "heure":  last_heure
    })


# ================= MAIN =================
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
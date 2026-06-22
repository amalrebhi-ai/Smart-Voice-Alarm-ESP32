// lib/controllers/alarm_controller.dart
// Gère l'état de l'alarme, le countdown et le déclenchement

import 'dart:async';
import 'package:flutter/material.dart';
import 'alarm_command.dart';
import 'alarm_service.dart';

enum AlarmStatus {
  idle,        // Aucune alarme
  scheduled,   // Alarme programmée (countdown actif)
  ringing,     // Alarme en train de sonner !
  deactivated, // Alarme désactivée manuellement
}

class AlarmController extends ChangeNotifier {
  // ── État courant ──────────────────────────────────────────
  AlarmStatus status = AlarmStatus.idle;
  AlarmCommand? lastCommand;
  DateTime? alarmTime;
  Duration? remainingTime;
  bool isConnected = true;
  String? errorMessage;

  // ── Timers internes ───────────────────────────────────────
  Timer? _pollingTimer;   // Interroge le serveur toutes les 2s
  Timer? _countdownTimer; // Met à jour le countdown chaque seconde
  Timer? _ringTimer;      // Arrête la sonnerie après 60s si pas stoppée

  // ── Démarrage ─────────────────────────────────────────────
  void start() {
    _startPolling();
  }

  // ── Polling Flask ─────────────────────────────────────────
  void _startPolling() {
    _pollingTimer?.cancel();
    _pollingTimer = Timer.periodic(const Duration(seconds: 2), (_) async {
      final cmd = await AlarmService.fetchLastCommand();
      if (cmd == null) {
        isConnected = false;
        errorMessage = 'Serveur introuvable – vérifiez l\'IP et le Wi-Fi';
        notifyListeners();
        return;
      }
      isConnected = true;
      errorMessage = null;
      _handleCommand(cmd);
    });
  }

  // ── Traitement de la commande reçue ───────────────────────
  void _handleCommand(AlarmCommand cmd) {
    // Ignorer si la commande n'a pas changé
    if (cmd.action == lastCommand?.action && cmd.heure == lastCommand?.heure) {
      return;
    }
    lastCommand = cmd;

    switch (cmd.action) {
      case 'set_alarm':
        _scheduleAlarm(cmd);
        break;



      case 'deactivate_alarm':
        _deactivateAlarm();
        break;



      default:
        // Pas d'action reconnue
        break;
    }
    notifyListeners();
  }

  // ── Programmer l'alarme ───────────────────────────────────
  void _scheduleAlarm(AlarmCommand cmd) {
    final target = cmd.alarmDateTime;
    if (target == null) return;

    alarmTime = target;
    status = AlarmStatus.scheduled;
    _startCountdown(target);
  }

  void _startCountdown(DateTime target) {
    _countdownTimer?.cancel();
    _countdownTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      final now = DateTime.now();
      final diff = target.difference(now);

      if (diff.isNegative || diff.inSeconds == 0) {
        _triggerRinging();
      } else {
        remainingTime = diff;
        notifyListeners();
      }
    });
  }

  // ── Déclencher la sonnerie ────────────────────────────────
  void _triggerRinging() {
    _countdownTimer?.cancel();
    remainingTime = Duration.zero;
    status = AlarmStatus.ringing;
    notifyListeners();

    // Arrêt automatique après 60 secondes
    _ringTimer?.cancel();
    _ringTimer = Timer(const Duration(seconds: 60), () {
      if (status == AlarmStatus.ringing) {
        _deactivateAlarm();
      }
    });
  }

  // ── Désactiver l'alarme ───────────────────────────────────
  void _deactivateAlarm() {
    _countdownTimer?.cancel();
    _ringTimer?.cancel();
    status = AlarmStatus.deactivated;
    remainingTime = null;
    notifyListeners();

    // Retour à idle après 4 secondes
    Timer(const Duration(seconds: 4), () {
      status = AlarmStatus.idle;
      alarmTime = null;
      notifyListeners();
    });
  }

  // ── Arrêt manuel (bouton Stop dans l'UI) ──────────────────
  void stopAlarm() {
    _deactivateAlarm();
  }

  // ── Formatage ─────────────────────────────────────────────
  String get countdownString {
    if (remainingTime == null) return '--:--:--';
    final h = remainingTime!.inHours;
    final m = remainingTime!.inMinutes.remainder(60);
    final s = remainingTime!.inSeconds.remainder(60);
    return '${h.toString().padLeft(2, '0')}:'
        '${m.toString().padLeft(2, '0')}:'
        '${s.toString().padLeft(2, '0')}';
  }

  String get alarmTimeString {
    if (alarmTime == null) return '';
    final h = alarmTime!.hour.toString().padLeft(2, '0');
    final m = alarmTime!.minute.toString().padLeft(2, '0');
    return '$h:$m';
  }

  @override
  void dispose() {
    _pollingTimer?.cancel();
    _countdownTimer?.cancel();
    _ringTimer?.cancel();
    super.dispose();
  }
}

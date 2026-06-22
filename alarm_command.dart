// lib/models/alarm_command.dart
// Modèle représentant la réponse JSON du serveur Flask

class AlarmCommand {
  final String texte;
  final String? action;
  final String? heure;

  AlarmCommand({
    required this.texte,
    this.action,
    this.heure,
  });

  factory AlarmCommand.fromJson(Map<String, dynamic> json) {
    return AlarmCommand(
      texte: json['texte'] ?? '',
      action: json['action'],
      heure: json['heure'],
    );
  }

  /// Retourne true si la commande est non vide
  bool get hasContent => texte.isNotEmpty || action != null;

  /// Convertit l'heure "HH:MM" en DateTime aujourd'hui (ou demain si déjà passée)
  DateTime? get alarmDateTime {
    if (heure == null) return null;
    final parts = heure!.split(':');
    if (parts.length != 2) return null;
    final h = int.tryParse(parts[0]);
    final m = int.tryParse(parts[1]);
    if (h == null || m == null) return null;

    final now = DateTime.now();
    var alarm = DateTime(now.year, now.month, now.day, h, m);
    // Si l'heure est déjà passée aujourd'hui → programmer pour demain
    if (alarm.isBefore(now)) {
      alarm = alarm.add(const Duration(days: 1));
    }
    return alarm;
  }

  @override
  String toString() =>
      'AlarmCommand(texte: $texte, action: $action, heure: $heure)';
}

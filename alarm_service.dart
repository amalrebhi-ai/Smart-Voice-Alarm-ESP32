// lib/services/alarm_service.dart
// Service HTTP qui interroge le serveur Flask toutes les 2 secondes

import 'dart:convert';
import 'package:http/http.dart' as http;
import 'alarm_command.dart';

class AlarmService {
  //  Modifie cette IP selon ton réseau local
  static const String _baseUrl = 'http://192.168.202.109:5000';

  /// Récupère la dernière commande du serveur Flask via GET /command
  static Future<AlarmCommand?> fetchLastCommand() async {
    try {
      final response = await http
          .get(Uri.parse('$_baseUrl/command'))
          .timeout(const Duration(seconds: 3));

      if (response.statusCode == 200) {
        final json = jsonDecode(response.body) as Map<String, dynamic>;
        return AlarmCommand.fromJson(json);
      }
    } catch (e) {
      // Connexion échouée (serveur éteint, mauvaise IP, timeout…)
      return null;
    }
    return null;
  }
}

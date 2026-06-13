class ToyotaScenePresets:
  # accel_profile: sport = 0, normal = 1, eco = 2, stock = 3
  # AccelPersonality: sport = 0, normal = 1, eco = 2, stock = 3
  # LongitudinalPersonality: aggressive = 0, standard = 1, relaxed = 2

  @staticmethod
  def get_preset(accel_profile):
    """
    Maps Toyota accelProfile to (accel_personality, longitudinal_personality, label_name)
    """
    if accel_profile == 0:  # Power/Sport -> 山路
      return 2, 1, "山路"   # AccelPersonality.eco, LongitudinalPersonality.standard
    elif accel_profile == 1:  # Normal -> 市區
      return 1, 0, "市區"   # AccelPersonality.normal, LongitudinalPersonality.aggressive
    elif accel_profile == 2:  # Eco -> 高速
      return 2, 0, "高速"   # AccelPersonality.eco, LongitudinalPersonality.aggressive
    
    return None, None, None

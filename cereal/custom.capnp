using Cxx = import "./include/c++.capnp";
$Cxx.namespace("cereal");

@0xb526ba661d550a59;

# custom.capnp: a home for empty structs reserved for custom forks
# These structs are guaranteed to remain reserved and empty in mainline
# cereal, so use these if you want custom events in your fork.

# DO rename the structs
# DON'T change the identifier (e.g. @0x81c2f05a394cf4af)

struct TopControlsState @0x81c2f05a394cf4af {
  alkaActive @0 :Bool;
}

struct LongitudinalPlanTOP @0xaedffd8f31e7b55d {
  accelPersonality @0 :AccelerationPersonality;
  longitudinalPlanSource @1 :LongitudinalPlanSource;
  smartCruiseControl @2 :SmartCruiseControl;
  speedLimit @3 :SpeedLimit;
  vTarget @4 :Float32;
  aTarget @5 :Float32;
  brakeAssist @6 :BrakeAssist;

  struct BrakeAssist {
    state @0 :State;
    active @1 :Bool;
    confidenceBand @2 :ConfidenceBand;
    vTarget @3 :Float32;
    aTarget @4 :Float32;
    shouldStop @5 :Bool;
    allowThrottle @6 :Bool;
    targetDistanceM @7 :Float32;
    source @8 :Text;
    releaseReason @9 :Text;

    enum State {
      inactive @0;
      prepare @1;
      brake @2;
      hardBrake @3;
      finalStop @4;
    }

    enum ConfidenceBand {
      none @0;
      low @1;
      mid @2;
      high @3;
    }
  }

  enum AccelerationPersonality {
    sport @0;
    normal @1;
    eco @2;
    stock @3;
  }

  struct SmartCruiseControl {
    vision @0 :Vision;
    map @1 :Map;

    struct Vision {
      state @0 :VisionState;
      vTarget @1 :Float32;
      aTarget @2 :Float32;
      currentLateralAccel @3 :Float32;
      maxPredictedLateralAccel @4 :Float32;
      enabled @5 :Bool;
      active @6 :Bool;
    }

    struct Map {
      state @0 :MapState;
      vTarget @1 :Float32;
      aTarget @2 :Float32;
      enabled @3 :Bool;
      active @4 :Bool;
    }

    enum VisionState {
      disabled @0; # System disabled or inactive.
      enabled @1; # No predicted substantial turn on vision range.
      entering @2; # A substantial turn is predicted ahead, adapting speed to turn comfort levels.
      turning @3; # Actively turning. Managing acceleration to provide a roll on turn feeling.
      leaving @4; # Road ahead straightens. Start to allow positive acceleration.
      overriding @5; # System overriding with manual control.
    }

    enum MapState {
      disabled @0; # System disabled or inactive.
      enabled @1; # No predicted substantial turn on map range.
      turning @2; # Actively turning. Managing acceleration to provide a roll on turn feeling.
      overriding @3; # System overriding with manual control.
    }
  }

  struct SpeedLimit {
    resolver @0 :Resolver;
    assist @1 :Assist;

    struct Resolver {
      speedLimit @0 :Float32;
      distToSpeedLimit @1 :Float32;
      source @2 :Source;
      speedLimitOffset @3 :Float32;
      speedLimitLast @4 :Float32;
      speedLimitFinal @5 :Float32;
      speedLimitFinalLast @6 :Float32;
      speedLimitValid @7 :Bool;
      speedLimitLastValid @8 :Bool;
    }

    struct Assist {
      state @0 :AssistState;
      enabled @1 :Bool;
      active @2 :Bool;
      vTarget @3 :Float32;
      aTarget @4 :Float32;
    }

    enum Source {
      none @0;
      car @1;
      map @2;
    }

    enum AssistState {
      disabled @0;
      inactive @1; # No speed limit set or not enabled by parameter.
      preActive @2;
      pending @3; # Awaiting new speed limit.
      adapting @4; # Reducing speed to match new speed limit.
      active @5; # Cruising at speed limit.
    }
  }

  enum LongitudinalPlanSource {
    cruise @0;
    sccVision @1;
    sccMap @2;
    speedLimitAssist @3;
    brakeAssist @4;
  }
}

struct ModelExt @0xf35cc4560bbf6ec2 {
  leftEdgeDetected @0 :Bool;
  rightEdgeDetected @1 :Bool;
}

struct LiveMapDataTOP @0xda96579883444c35 {
  speedLimitValid @0 :Bool;
  speedLimit @1 :Float32;
  speedLimitAheadValid @2 :Bool;
  speedLimitAhead @3 :Float32;
  speedLimitAheadDistance @4 :Float32;
  roadName @5 :Text;
}

struct CarStateTOP @0x80ae746ee2596b11 {
  speedLimit @0 :Float32;
}

struct FridayTrafficIntent @0xa5cd762cd951a455 {
  color @0 :Color;
  confidence @1 :Float32;
  source @2 :Text;
  ageS @3 :Float32;
  stale @4 :Bool;
  range @5 :Range;
  reason @6 :Text;

  enum Color {
    unknown @0;
    red @1;
    mixed @2;
    green @3;
  }

  enum Range {
    unknown @0;
    far @1;
    mid @2;
    near @3;
  }
}

struct FridayIntersectionDistance @0xf98d843bfd7004a3 {
  active @0 :Bool;
  distanceM @1 :Float32;
  confidence @2 :Float32;
  source @3 :Text;
  ageS @4 :Float32;
  stale @5 :Bool;
  failReason @6 :Text;
  computeMs @7 :Float32;
}

struct CustomReserved7 @0xb86e6369214c01c8 {
}

struct CustomReserved8 @0xf416ec09499d9d19 {
}

struct CustomReserved9 @0xa1680744031fdb2d {
}

struct CustomReserved10 @0xcb9fd56c7057593a {
}

struct CustomReserved11 @0xc2243c65e0340384 {
}

struct CustomReserved12 @0x9ccdc8676701b412 {
}

struct CustomReserved13 @0xcd96dafb67a082d0 {
}

struct CustomReserved14 @0xb057204d7deadf3f {
}

struct CustomReserved15 @0xbd443b539493bc68 {
}

struct CustomReserved16 @0xfc6241ed8877b611 {
}

struct CustomReserved17 @0xa30662f84033036c {
}

struct CustomReserved18 @0xc86a3d38d13eb3ef {
}

struct CustomReserved19 @0xa4f1eb3323f5f582 {
}

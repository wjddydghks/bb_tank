#include <Servo.h>

/* ================= 핀 ================= */
#define SERVO_LEFT   13
#define SERVO_RIGHT  12

#define TRIG_R 11
#define ECHO_R 10
#define TRIG_L 9
#define ECHO_L 8

#define SERVO_YAW   5    // 포탑 좌우
#define FIRE_PIN    7

/* ================= 서보 ================= */
Servo servoLeft, servoRight;
Servo servoYaw;

/* ================= 상태 ================= */
enum Mode { PATROL, ATTACK, RESET_TURRET };
Mode mode = PATROL;

/* ================= 시간 ================= */
unsigned long lastUltraTime = 0;
unsigned long lastTargetTime = 0;

/* ================= 발사 쿨다운 ================= */
unsigned long fireTime = 0;
const unsigned long FIRE_COOLDOWN = 3000;
bool inFireCooldown = false;

/* ================= 타겟 ================= */
int xErr = 0;
bool locked = false;
bool hasTarget = false;

/* ================= 포탑 ================= */
int currentYaw = 90;

/* ================= 주행 ================= */
// 전진 (너가 준 값 그대로)
void forward() {
  servoLeft.writeMicroseconds(1300);
  servoRight.writeMicroseconds(1700);
}

void backward() {
  servoLeft.writeMicroseconds(1700);
  servoRight.writeMicroseconds(1300);
}

void stopMove() {
  servoLeft.writeMicroseconds(1500);
  servoRight.writeMicroseconds(1500);
}

/*
 ⚠️ 중요
 실제 테스트 기준:
 - 아래 turnLeft()  → 차체가 "왼쪽"으로 회전
 - 아래 turnRight() → 차체가 "오른쪽"으로 회전
*/
void turnLeft() {     // ← 왼쪽 회전
  servoLeft.writeMicroseconds(1500);
  servoRight.writeMicroseconds(1700);
}

void turnRight() {    // → 오른쪽 회전
  servoLeft.writeMicroseconds(1300);
  servoRight.writeMicroseconds(1500);
}

/* ================= 초음파 ================= */
long readUltra(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long t = pulseIn(echo, HIGH, 25000);
  if (t == 0) return 999;
  return t / 58;
}

/* ================= 포탑 ================= */
void stopTurret() {
  servoYaw.writeMicroseconds(1500);
}

void aimTurret() {
  // 중앙 범위 넓게
  if (abs(xErr) < 60) {
    stopTurret();
    return;
  }

  int speed = 12; // 느리게 (조준 안정)
  if (xErr > 0) {
    servoYaw.writeMicroseconds(1500 + speed);
    currentYaw++;
  } else {
    servoYaw.writeMicroseconds(1500 - speed);
    currentYaw--;
  }

  currentYaw = constrain(currentYaw, 0, 180);
}

void resetTurret() {
  if (abs(currentYaw - 90) < 4) {
    stopTurret();
    currentYaw = 90;
    mode = PATROL;
    return;
  }

  if (currentYaw > 90) {
    servoYaw.writeMicroseconds(1485);
    currentYaw--;
  } else {
    servoYaw.writeMicroseconds(1515);
    currentYaw++;
  }
}

/* ================= 발사 ================= */
void fire() {
  digitalWrite(FIRE_PIN, HIGH);
  delay(300);
  digitalWrite(FIRE_PIN, LOW);

  fireTime = millis();
  inFireCooldown = true;

  Serial.println(">>> FIRED");
}

/* ================= 시리얼 ================= */
void readSerial() {
  if (inFireCooldown) return;
  if (!Serial.available()) return;

  String data = Serial.readStringUntil('\n');
  data.trim();

  if (data == "NONE") {
    hasTarget = false;
    locked = false;
    return;
  }

  int a = data.indexOf(',');
  int b = data.lastIndexOf(',');
  if (a == -1 || b == -1) return;

  xErr = data.substring(0, a).toInt();
  locked = (data.substring(b + 1) == "T");

  hasTarget = true;
  lastTargetTime = millis();
  mode = ATTACK;
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);

  servoLeft.attach(SERVO_LEFT);
  servoRight.attach(SERVO_RIGHT);
  servoYaw.attach(SERVO_YAW);

  pinMode(TRIG_L, OUTPUT);
  pinMode(ECHO_L, INPUT);
  pinMode(TRIG_R, OUTPUT);
  pinMode(ECHO_R, INPUT);

  pinMode(FIRE_PIN, OUTPUT);
  digitalWrite(FIRE_PIN, LOW);

  stopMove();
  stopTurret();

  Serial.println("BB TANK READY");
}

/* ================= LOOP ================= */
void loop() {
  unsigned long now = millis();

  readSerial();

  /* ===== 발사 쿨다운 ===== */
  if (inFireCooldown) {
    stopMove();
    resetTurret();

    if (now - fireTime > FIRE_COOLDOWN) {
      inFireCooldown = false;
      hasTarget = false;
      locked = false;
      mode = PATROL;
    }
    return;
  }

  /* ===== 타겟 타임아웃 ===== */
  if (hasTarget && now - lastTargetTime > 600) {
    hasTarget = false;
    locked = false;
    stopTurret();
    mode = PATROL;
  }

  /* ===== 초음파 회피 (정상 방향) ===== */
  if (mode == PATROL && now - lastUltraTime > 80) {
    lastUltraTime = now;

    long dL = readUltra(TRIG_L, ECHO_L);
    long dR = readUltra(TRIG_R, ECHO_R);

    if (dL < 25 && dR >= 25) {
      // 왼쪽 장애물 → 오른쪽 회전
      turnRight();
      delay(300);
    }
    else if (dR < 25 && dL >= 25) {
      // 오른쪽 장애물 → 왼쪽 회전
      turnLeft();
      delay(300);
    }
    else if (dL < 25 && dR < 25) {
      // 정면 막힘
      backward();
      delay(400);
    }
  }

  /* ===== 상태 ===== */
  switch (mode) {
    case PATROL:
      forward();
      break;

    case ATTACK:
      stopMove();
      aimTurret();

      if (locked) {
        stopTurret();
        delay(200);
        fire();
        mode = RESET_TURRET;
      }
      break;

    case RESET_TURRET:
      resetTurret();
      break;
  }
}

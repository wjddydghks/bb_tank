#include <Servo.h>

/* ================= 핀 ================= */
#define SERVO_LEFT   13
#define SERVO_RIGHT  12

#define TRIG_L 9
#define ECHO_L 8
#define TRIG_R 11
#define ECHO_R 10

#define SERVO_YAW 5
#define FIRE_PIN 7

/* ================= 서보 ================= */
Servo servoLeft, servoRight;
Servo servoYaw;

/* ================= 상수 ================= */
#define STOP_US   1500

#define FWD_L     1300
#define FWD_R     1700

#define BACK_L    1700
#define BACK_R    1300

#define TURN_L_L  1500
#define TURN_L_R  1700

#define TURN_R_L  1300
#define TURN_R_R  1500

// 🔥 YAW 매우 느리게 (중요)
#define YAW_RIGHT 1510
#define YAW_LEFT  1490

#define AIM_TIME      800   // 조준 유지 시간 (늘림)
#define FIRE_TIME     300
#define COOLDOWN_TIME 3000  // 발사 후 강제 복귀 시간

/* ================= 상태 ================= */
bool firing = false;
bool wheelsDetached = false;

unsigned long stepTime = 0;
unsigned long fireCooldownStart = 0;

int fireStep = 0;

/* ================= 바퀴 제어 ================= */
void forward() {
  servoLeft.writeMicroseconds(FWD_L);
  servoRight.writeMicroseconds(FWD_R);
}
void backward() {
  servoLeft.writeMicroseconds(BACK_L);
  servoRight.writeMicroseconds(BACK_R);
}
void stopMove() {
  servoLeft.writeMicroseconds(STOP_US);
  servoRight.writeMicroseconds(STOP_US);
}
void turnLeft() {
  servoLeft.writeMicroseconds(TURN_L_L);
  servoRight.writeMicroseconds(TURN_L_R);
}
void turnRight() {
  servoLeft.writeMicroseconds(TURN_R_L);
  servoRight.writeMicroseconds(TURN_R_R);
}

/* ================= 바퀴 분리 ================= */
void detachWheels() {
  if (!wheelsDetached) {
    servoLeft.detach();
    servoRight.detach();
    wheelsDetached = true;
  }
}
void attachWheels() {
  if (wheelsDetached) {
    servoLeft.attach(SERVO_LEFT);
    servoRight.attach(SERVO_RIGHT);
    stopMove();
    wheelsDetached = false;
  }
}

/* ================= 초음파 ================= */
long readUltra(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long t = pulseIn(echo, HIGH, 25000);
  if (t == 0) return 999;
  return t / 58;
}

/* ================= 발사 ================= */
void fireOn()  { digitalWrite(FIRE_PIN, HIGH); }
void fireOff() { digitalWrite(FIRE_PIN, LOW); }

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
  fireOff();

  stopMove();
  servoYaw.writeMicroseconds(STOP_US);

  randomSeed(analogRead(A0));

  Serial.println("BB TANK FINAL READY");
}

/* ================= LOOP ================= */
void loop() {
  unsigned long now = millis();

  /* ===== 발사 쿨다운 ===== */
  if (fireCooldownStart > 0) {
    detachWheels();
    servoYaw.writeMicroseconds(STOP_US);

    if (now - fireCooldownStart > COOLDOWN_TIME) {
      fireCooldownStart = 0;
      attachWheels();
      Serial.println("Cooldown end");
    }
    return;
  }

  /* ===== FIRE 명령 ===== */
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "FIRE" && !firing) {
      firing = true;
      fireStep = 0;
      Serial.println("FIRE START");
    }
  }

  /* ===== 발사 시퀀스 ===== */
  if (firing) {
    detachWheels();   // 🔥 핵심: 몸체 고정

    switch (fireStep) {
      case 0: // 오른쪽 조준
        servoYaw.writeMicroseconds(YAW_RIGHT);
        stepTime = now;
        fireStep = 1;
        break;

      case 1: // 조준 유지
        if (now - stepTime > AIM_TIME) {
          servoYaw.writeMicroseconds(STOP_US);
          stepTime = now;
          fireStep = 2;
        }
        break;

      case 2: // 발사
        fireOn();
        delay(FIRE_TIME);
        fireOff();
        stepTime = now;
        fireStep = 3;
        break;

      case 3: // 복귀
        servoYaw.writeMicroseconds(YAW_LEFT);
        if (now - stepTime > AIM_TIME) {
          servoYaw.writeMicroseconds(STOP_US);
          firing = false;
          fireCooldownStart = millis(); // 강제 복귀 타임
          Serial.println("FIRE DONE");
        }
        break;
    }
    return;
  }

  /* ===== 자율 주행 ===== */
  long dL = readUltra(TRIG_L, ECHO_L);
  long dR = readUltra(TRIG_R, ECHO_R);

  if (dL < 20 && dR < 20) {
    backward();
    delay(300);
    if (random(0, 2) == 0) turnLeft();
    else turnRight();
    delay(400);
  }
  else if (dL < 20) {
    // 왼쪽 장애물 → 오른쪽 회피
    turnRight();
  }
  else if (dR < 20) {
    // 오른쪽 장애물 → 왼쪽 회피 (수정 완료)
    turnLeft();
  }
  else {
    forward();
  }
}

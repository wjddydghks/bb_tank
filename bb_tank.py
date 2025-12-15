import cv2
import serial
import time
from ultralytics import YOLO


class BBTankComplete:
    def __init__(self, port='/dev/ttyACM0', baudrate=115200):
        print("=== BB탱크 자동 조준 시스템 시작 ===\n")

        # YOLO
        print("YOLO 로딩...")
        self.model = YOLO('yolov8n.pt')  # n 모델 (빠름)
        print("YOLO 로드 완료\n")

        # 시리얼
        try:
            self.serial = serial.Serial(port, baudrate, timeout=0.01)
            time.sleep(2)
            print(f"시리얼 연결 완료: {self.serial.name}\n")
        except:
            print("시리얼 연결 실패\n")
            self.serial = None

        self.frame_width = 640
        self.frame_height = 480
        self.center_x = 320
        self.center_y = 240
        self.dead_zone = 50

        # 이전 프레임 사람 정보
        self.last_persons = []

    def setup_camera(self):
        pipeline = (
            "nvarguscamerasrc sensor-id=0 ! "
            "video/x-raw(memory:NVMM), width=640, height=480, framerate=30/1 ! "
            "nvvidconv ! video/x-raw, format=BGRx ! "
            "videoconvert ! video/x-raw, format=BGR ! "
            "appsink"
        )

        print("카메라 초기화...")
        cap = cv2.VideoCapture(pipeline, cv2.CAP_GSTREAMER)

        if not cap.isOpened():
            print("카메라 열기 실패!")
            return None

        print("카메라 준비 완료!\n")
        time.sleep(2)
        return cap

    def send_to_arduino(self, x_err, y_err, locked):
        if self.serial and self.serial.is_open:
            flag = "T" if locked else "N"
            cmd = f"{x_err},{y_err},{flag}\n"
            self.serial.write(cmd.encode())

    def send_none(self):
        if self.serial and self.serial.is_open:
            self.serial.write(b"NONE\n")

    def run(self):
        cap = self.setup_camera()
        if cap is None:
            print("카메라 오류로 종료")
            return

        print("=" * 60)
        print("BB탱크 자동 조준 시작!")
        print("=" * 60)
        print()

        frame_count = 0
        detection_count = 0
        start_time = time.time()

        try:
            while True:
                ret, frame = cap.read()
                if not ret:
                    continue

                frame_count += 1
                display = frame.copy()

                # ========== YOLO 탐지 (2프레임마다) ==========
                if frame_count % 2 == 0:
                    results = self.model(frame, verbose=False)

                    current_persons = []

                    for box in results[0].boxes:
                        class_id = int(box.cls[0])

                        if class_id == 0:  # person
                            conf = float(box.conf[0])
                            if conf < 0.35:
                                continue

                            x1, y1, x2, y2 = box.xyxy[0].cpu().numpy()
                            x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)

                            current_persons.append((x1, y1, x2, y2, conf))

                    self.last_persons = current_persons

                all_persons = self.last_persons

                # 박스 표시
                for x1, y1, x2, y2, conf in all_persons:
                    cv2.rectangle(display, (x1, y1), (x2, y2), (0, 255, 255), 4)
                    cv2.putText(display, f"person {conf:.2f}",
                                (x1, y1 - 10),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                                (0, 255, 255), 2)

                # ========== 타겟 처리 ==========
                if all_persons:
                    detection_count += 1

                    largest = max(
                        all_persons,
                        key=lambda p: (p[2] - p[0]) * (p[3] - p[1])
                    )

                    x1, y1, x2, y2, conf = largest

                    x_center = (x1 + x2) // 2
                    y_center = (y1 + y2) // 2

                    x_err = x_center - self.center_x
                    y_err = y_center - self.center_y

                    locked = (
                        abs(x_err) < self.dead_zone and
                        abs(y_err) < self.dead_zone
                    )

                    self.send_to_arduino(x_err, y_err, locked)

                    cv2.rectangle(display, (x1, y1), (x2, y2), (0, 0, 255), 8)
                    cv2.circle(display, (x_center, y_center), 35, (0, 0, 255), 5)
                    cv2.circle(display, (x_center, y_center), 12, (0, 0, 255), -1)
                    cv2.line(display,
                             (self.center_x, self.center_y),
                             (x_center, y_center),
                             (0, 255, 0), 4)

                    if locked:
                        cv2.putText(display, "LOCKED ON!",
                                    (x_center - 90, y_center - 55),
                                    cv2.FONT_HERSHEY_SIMPLEX,
                                    1.7, (0, 0, 255), 5)
                        cv2.putText(display, ">>> FIRING! <<<",
                                    (130, 410),
                                    cv2.FONT_HERSHEY_SIMPLEX,
                                    2.0, (0, 0, 255), 6)
                    else:
                        cv2.putText(display, "TRACKING",
                                    (x_center - 75, y_center - 55),
                                    cv2.FONT_HERSHEY_SIMPLEX,
                                    1.3, (0, 255, 255), 4)

                else:
                    self.send_none()
                    cv2.putText(display, "SEARCHING...",
                                (170, 260),
                                cv2.FONT_HERSHEY_SIMPLEX,
                                1.8, (0, 165, 255), 5)

                # 중앙 십자
                cv2.line(display,
                         (self.center_x - 55, self.center_y),
                         (self.center_x + 55, self.center_y),
                         (0, 255, 0), 4)
                cv2.line(display,
                         (self.center_x, self.center_y - 55),
                         (self.center_x, self.center_y + 55),
                         (0, 255, 0), 4)
                cv2.circle(display,
                           (self.center_x, self.center_y),
                           self.dead_zone,
                           (0, 255, 0), 3)

                fps = frame_count / (time.time() - start_time)
                cv2.putText(display, f"FPS: {fps:.1f}",
                            (10, 45),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            1.4, (0, 255, 0), 4)

                cv2.imshow('BB Tank - Auto Aim System', display)

                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break

        finally:
            cap.release()
            cv2.destroyAllWindows()
            if self.serial:
                self.send_none()
                self.serial.close()


if __name__ == "__main__":
    import os

    os.system("sudo systemctl restart nvargus-daemon")
    time.sleep(3)

    tank = BBTankComplete(port='/dev/ttyACM0', baudrate=115200)
    tank.run()

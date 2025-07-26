# 💉 Smart Saline Infusion Care Unit

An intelligent IoT-based system to monitor and control intravenous (IV) saline infusion in real-time, ensuring patient safety and optimizing hospital staff workload. The system measures patient vitals, automates drip rate based on conditions, and prevents overflow or reverse flow.

---

## 📌 Project Overview

This smart infusion care system uses an ESP32 microcontroller integrated with various biomedical and environmental sensors. It continuously tracks saline levels, patient vitals, and room conditions, sending data to the cloud and alerting medical staff in critical situations.

Designed to improve accuracy, reduce human error, and optimize IV administration in both hospital and homecare environments.

---

## ⚙️ Features

- 🔄 Real-time IV drip monitoring
- 📡 IoT integration with cloud (ThingSpeak)
- 🫀 Vital tracking: SpO₂, heart rate, body & room temperature
- 🧠 AI-based adaptive drip control (via servo motor)
- 🚨 Alerts for overflow, dry run, or backflow
- 📟 LCD interface with keypad for patient/hospital mode
- 💡 Auto bubble detection mechanism *(Fresenius Kabi enhancement)*

---

## 🧠 Hardware Components

- **NodeMCU ESP32**
- **MAX30102** – SpO₂ and Heart Rate Sensor
- **DS18B20** – Body Temperature Sensor
- **DHT11** – Room Temperature & Humidity Sensor
- **HX711 + Load Cell** – Saline Bottle Weight Monitoring
- **16x2 LCD Display + 4x4 Keypad**
- **Servo Motor** – Dynamic drip rate adjustment
- **Buzzer + LED Indicators**
- **Wi-Fi Connectivity** for cloud-based monitoring

---

## 🧾 Working Principle

1. User selects **Patient Mode** or **Hospital Mode** via keypad.
2. System reads vitals and saline weight in real time.
3. If bottle is nearly empty or drip is too fast/slow:
   - Alerts via buzzer & LED
   - Data sent to cloud (ThingSpeak)
4. Servo motor auto-adjusts drip rate based on patient temperature or pressure.
5. Safety checks include reverse flow and bubble detection (enhanced prototype).

---

## 🌐 IoT Integration

- **Cloud Platform:** ThingSpeak
- **Data Sent:**
  - Saline level (from load cell)
  - SpO₂ and Heart Rate
  - Temperature (body + room)
  - Drip rate and alerts
- **Dashboard:** Real-time visualization for doctors & caregivers

---

## 📁 File Structure

┣ 📜 smart_saline_unit.ino # Main Arduino code
┣ 📜 README.md # Project documentation
┣ 📸 images/ # Circuit photos, UI screenshots
┗ 📄 Report.pdf # Full project report


---

## 📸 Prototype Snapshots

> Add circuit/build images here:
> ![Prototype](images/smart-saline-demo.jpg)

---

## 🔧 Future Enhancements

- GSM fallback for no-WiFi alerting
- Mobile App integration (Flutter/React)
- Auto-calibration of sensors
- Integration with hospital database systems (EHRs)

---

## 🏆 Achievements

- Finalist at **DIPEX 2025** – State-Level Project Expo  
- Presented to **Fresenius Kabi** team and **Exelia product founder**  
- Recognized for innovation in **patient safety automation**

---

## 👩‍💻 Author

**Snehal Navanath Lokhande**  
Electronics & Telecommunication, COEP Technological University  
[LinkedIn](https://www.linkedin.com/in/snehal-lokhande-376b27357)

---

## 📄 License

This project is open-source and licensed under the [MIT License](https://choosealicense.com/licenses/mit/).


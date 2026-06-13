# AEGIS Fork 功能稽核 (Fork Audit)

本頁面記錄了其他開源 openpilot 分支（Forks）中值得後續研究、移植或借鑒的功能特色，作為 AEGIS 未來發展的規劃參考。

---

## 1. FrogPilot
* **Alert Volume Controller (警示音量控制器)**：可獨立調節或微調各類 openpilot 提示與警告音量，避免提示音過於刺耳。
* **Custom Following/Jerk (自訂跟車/減震參數)**：允許使用者微調跟車距離反應時間 (reaction time) 以及加減速的加速度變化率 (jerk)，使舒適度更貼近個人偏好。
* **Custom Speed Interval (自訂速度區間)**：提供更靈活的速度控制與速限偏置設定。

## 2. sunnypilot / animalpilot
* **Quiet Drive (安靜駕駛)**：類似功能，提供更高自訂性的警示音過濾（僅保留關鍵安全提示）。
* **Gap Adjust Cruise (跟車距離微調)**：允許透過方向盤按鈕快速微調不同速域下的安全車距。
* **MADS (Multi-Active Drive Assistance System)**：橫向控制（車道維持）與縱向控制（定速/跟車）完全獨立。在煞車或手動控制油門時仍能維持 LKAS（車道維持），提供更不中斷的輔助體驗。

## 3. DragonPilot
* **Toyota/Lexus 專項功能研究**：
  - 深入研究 Toyota 既有車端安全硬體與 LKAS/LTA 的 CAN 控制訊號。
  - 後續可用於進一步最佳化 Toyota 車款在 AEGIS 中的深度整合與專屬設定。

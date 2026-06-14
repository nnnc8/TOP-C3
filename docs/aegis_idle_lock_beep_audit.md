# FRIDAY Idle Lock Beep Audit (怠速車外上鎖蜂鳴稽核計畫)

## 1. 稽核目標與背景
本計畫旨在確認「車輛於怠速（READY 狀態）下，從車外進行上鎖時產生的蜂鳴聲」之訊號來源。
目標是在不影響車子本身的鎖車安全、不偽造任何門鎖狀態、不干擾 openpilot 關鍵警告提示的前提下，分析是否能透過 openpilot 安全地抑制或降低該重疊蜂鳴聲。
* **基本原則**：本階段（V1 被動稽核）**只進行 CAN 訊號稽核與數據記錄**，不修改任何控制邏輯，不主動發送任何未經證實的 CAN 訊號。

---

## 2. 測試情境記錄表 (3 Scenarios)

| 情境 (Scenario) | 操作步驟 | 車外蜂鳴表現 | 預期觀察訊號與狀態 | 稽核記錄 (待填) |
| :--- | :--- | :--- | :--- | :--- |
| **1. 熄火正常車外鎖車** | 1. 熄火 (Power OFF)<br>2. 關閉所有車門<br>3. 從車外按下 Keyfob / 觸摸把手鎖車 | 正常單嗶聲 / 喇叭回饋 | `LOCKED_VIA_KEYFOB` = True<br>`LOCK_STATUS` = All Locked | |
| **2. 怠速車外鎖車 (蜂鳴發生)**| 1. 發動或維持 READY (怠速)<br>2. 關閉所有車門<br>3. 透過後改插件/實體鑰匙進行車外鎖車 | 長嗶聲 / 連續報警嗶聲 | `LOCK_STATUS_CHANGED`<br>`TWO_BEEPS`<br>`REPEATED_BEEPS` | |
| **3. 車內鎖車/解鎖 (Baseline)**| 1. 發動或維持 READY<br>2. 坐在車內，操作駕駛座中控鎖進行鎖車/解鎖 | 無蜂鳴聲 | `DOOR_LOCKS`<br>`LOCK_STATUS` | |

---

## 3. Toyota 相關 CAN 訊號清單 (Toyota CAN Signals Audit)
我們將在上述情境下錄製 CAN 紀錄，並重點過濾與稽核以下 Toyota DBC 訊號：

1. **`DOOR_LOCKS`** / **`LOCK_STATUS`** / **`LOCK_STATUS_CHANGED`**：
   * 監控車門鎖的實際物理狀態、中控鎖動作以及鎖定變更事件。
2. **`LOCKED_VIA_KEYFOB`**：
   * 物理鑰匙（Keyfob）鎖車的觸發狀態訊號。
3. **`CERTIFICATION_ECU`**：
   * 智慧鑰匙認證 ECU 與車身防盜系統的互動訊號。
4. **`DOOR_LOCK_FEEDBACK_LIGHT`** / **`KEYFOB_LOCKING_FEEDBACK_LIGHT`** / **`KEYFOB_UNLOCKING_FEEDBACK_LIGHT`**：
   * 車門上鎖與解鎖時的方向燈/雙閃回饋控制訊號。
5. **`TWO_BEEPS`** / **`REPEATED_BEEPS`**：
   * 車端既有的蜂鳴或 HUD 提示聲控制位元。
6. **`LANE_SWAY_BUZZER`**：
   * 橫向與車道偏移相關的安全蜂鳴器控制訊號。

---

## 4. 蜂鳴來源與控制分析 (Buzzer Source & Control Analysis)
在數據採集完成後，需根據以下三種可能來源進行歸類分析：

* **來源 A：由 openpilot 送出的既有 HUD/LDA 蜂鳴**
  * *判定特徵*：當執行怠速鎖車時，CAN 網路上有來自 openpilot 節點送出的蜂鳴 bit 設為 True。
  * *處置方案*：可直接在 `carcontroller.py` 中進行安全過濾與 suppress（抑制）。
* **來源 B：後改實體/軟體插件送出的 CAN 蜂鳴控制訊號**
  * *判定特徵*：CAN 網路上出現非 openpilot 且非原廠預期的 ID 在特定週期發送蜂鳴訊號。
  * *處置方案*：確認其 Address, Bus, Counter, Checksum 以及發送週期後，另開 feature 分支實作特定抑制。
* **來源 C：車身 Body ECU / Smart Key ECU 內部硬體電路直接驅動的蜂鳴**
  * *判定特徵*：CAN 網路上完全沒有任何對應的蜂鳴控制 bit 被發送，但物理上依然有發出蜂鳴聲。
  * *處置方案*：此為車身硬體直接驅動，openpilot 無法透過 CAN 網路關閉。僅記錄結論，不作任何強制控制。

---

## 5. 稽核判定與後續開發柵欄 (Implementation Gate)

本計畫設有嚴格的 **Gate（開發柵欄）** 限制：

1. **Gate 1：若判定為「來源 C（無可控輸出）」**
   * **結果**：**不新增任何控制功能**，本計畫到此結束，僅提交並保存本稽核文件與分析結論。
2. **Gate 2：若判定為「來源 A 或 B（有可控之 CAN 控制訊號）」**
   * **結果**：另行開啟 `feature/aegis-idle-lock-beep-suppress` 分支進行開發。
   * **抑制功能（Suppress Feature）規範**：
     * 必須新增持久參數：`AegisIdleLockBeepSuppress`，預設為 `false` (OFF)。
     * 抑制邏輯僅在以下**所有安全條件**同時成立時才會觸發：
       1. 車輛處於怠速（Engine Idle）或 READY 狀態下。
       2. 車速確認為零 (`vEgo == 0`)。
       3. 偵測到車外物理鎖車事件。
       4. 目前系統上無任何 openpilot 的主動 alert 警示。
     * **安全底線**：絕對不影響門鎖的實際狀態，不偽造任何 `lock_status`，以防造成安全隱憂。

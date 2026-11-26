# MYDOS 教學：線上留言簿

## 目標與成果

讓每個打開 MYDOS 網頁版的人，都能在 `C:\>` 打 `SIGN` 留下名字和一句話，下一個來的人就看得到。

```text
C:\>SIGN
Leave a message in the MYDOS guestbook. Everyone visiting the web
version can read it, so please be nice. (Ctrl+C to cancel)

Your name: Ada
Message:   Booted it on my laptop. Nice work!

Thanks, Ada! Saved to C:\GUEST\BOOK.TXT.
On the web version it is also sent to the online guestbook.

C:\>GUESTBOOK
MYDOS GUESTBOOK - type SIGN to leave a message
==============================================
2026-11-26 17:56  Ada
  Booted it on my laptop. Nice work!
```

桌面上也有一個「留言簿」視窗，可以正常顯示中文。

## 怎麼運作

GitHub Pages 只能放靜態檔案，沒辦法存訪客寫的東西，所以留言存在 [Supabase](https://supabase.com)（免費的線上 PostgreSQL 資料庫）。

```text
           讀                                         寫
 Supabase ──────▶ 網頁（guestbook.js）        MYDOS 的 SIGN 指令
              │                                  │
              ▼                                  ▼ 序列埠送出 ESC ] 7777 ; SIGN ; 名字 ; 留言 BEL
   開機前把留言填進 disk.img 裡              網頁攔截這個 OSC 序列
   C:\GUEST\BOOK.TXT 的預留區                     │
              │                                  ▼
              ▼                           POST 到 Supabase
   MYDOS 用 GUESTBOOK / TYPE 讀
```

### 為什麼說是「隔離」的

每個訪客的 C 槽都只存在自己的分頁裡，在 C 槽亂改、亂刪都不會影響別人。唯一會流到別人那邊的，是經過 `SIGN` 送出、而且通過資料庫規則檢查的留言。

### 寫：OSC 跳脫序列

MYDOS 跟網頁之間只有一條序列埠，所以要用一種「看不見的訊息」來傳話。終端機有一種叫 **OSC**（Operating System Command）的跳脫序列，格式是 `ESC ] 編號 ; 內容 BEL`，原本是讓程式設定視窗標題之類的事情用的。終端機遇到不認識的編號就直接忽略，不會顯示出來。

MYDOS 的 `SIGN` 送出 `ESC ] 7777 ; SIGN ; 名字 ; 留言 BEL`，網頁用 xterm.js 的 `registerOscHandler(7777, …)` 攔截。在 WSL 的 `make run` 裡，這段序列會被終端機直接忽略，留言只存在本機的 `BOOK.TXT`。

### 讀：就地修改磁碟映像

網頁不需要懂 FAT16。Makefile 在磁碟裡放了一個 32 KB 的 `C:\GUEST\BOOK.TXT`：開頭是兩行標題，後面用 `^Z`（0x1A，DOS 的檔案結尾記號）填滿。剛做好的映像，這 32 KB 在磁碟上是連續的。

網頁開機前在映像裡搜尋標題字串，就地把線上留言寫進去，剩下的位置繼續填 `^Z`，檔案大小不變。`TYPE` 讀到 `^Z` 就停，所以只會顯示留言。

## 安全設計

留言是陌生人寫的，處處都要假設它不懷好意：

| 威脅 | 防護 |
| --- | --- |
| 留言夾帶 ANSI 跳脫碼（清畫面、變色、偽造 OSC） | 寫進 `BOOK.TXT` 前只留可印的 ASCII，其他字元換成 `?` |
| 留言夾帶 HTML／JavaScript | 網頁視窗用 `textContent` 顯示，不會被當成 HTML |
| 竄改或刪除別人的留言 | Row Level Security 只允許 select 和 insert；update、delete 的權限全部收回 |
| 偽造留言時間 | 欄位權限：訪客只能填 `name` 和 `message`，`created_at` 由資料庫自己決定 |
| 超長、空白、含控制字元的留言 | 資料表的 check 限制（名字 1–20 字、留言 1–200 字） |
| 洗版 | 觸發器：整個留言簿每分鐘最多 20 則；網頁端也限制每 30 秒一則 |

這些規則我都在 PostgreSQL 上用 `anon` 角色實測過，包括模擬 Supabase「預設給 anon 全部權限」的情況。

網頁裡的 Supabase 金鑰本來就是公開的，每個訪客的瀏覽器都看得到。安全性靠的是上面這些資料庫規則，不是把金鑰藏起來。

**還是要做的事：** 這是公開留言簿，難免會有人亂寫。到 Supabase 的 **Table Editor → guestbook** 就能刪除不想要的留言。

## 設定步驟（大約 5 分鐘）

### 1. 建立 Supabase 專案

1. 到 https://supabase.com 用 GitHub 帳號登入。
2. **New project**：名稱隨意（例如 `mydos`），資料庫密碼按 Generate 並自己存好，Region 選 **Northeast Asia (Tokyo)** 或 **Southeast Asia (Singapore)**。
3. 等一兩分鐘建好。

### 2. 建立資料表和規則

1. 左邊選單 **SQL Editor** → **New query**。
2. 把 repo 裡 `docs/sql/guestbook.sql` 的內容整段貼上，按 **Run**，應該顯示 `Success. No rows returned`。

### 3. 把連線資料填進網站

1. 左邊選單 **Project Settings**：
   - **Data API**（或 **API**）頁面：複製 **Project URL**，例如 `https://abcdefghijklmnop.supabase.co`。
   - **API Keys** 頁面：複製 **Publishable key**（`sb_publishable_` 開頭）。舊專案可能只有 **anon public** key（`eyJ` 開頭），一樣可以用。
2. 打開 repo 的 `web/guestbook-config.js`，填進去：

```js
window.MYDOS_GUESTBOOK = {
    url: 'https://abcdefghijklmnop.supabase.co',
    anonKey: 'sb_publishable_xxxxxxxxxxxxxxxx',
};
```

**千萬不要**填 secret key（`sb_secret_` 開頭）或 service_role key，那兩把是管理員鑰匙。

3. commit、push，等 GitHub Actions 部署完成。

### 4. 測試

打開網站，在 MYDOS 裡打 `SIGN` 留一則言。狀態列應該出現 `Guestbook: thanks, …! Your message is online.`。重新整理後，打 `GUESTBOOK` 就看得到。

**常見問題：**

| 症狀 | 原因 |
| --- | --- |
| 留言簿視窗寫「還沒有設定」 | `guestbook-config.js` 沒填，或還沒部署 |
| `could not send (HTTP 401)` | 金鑰貼錯，或貼成 secret key |
| `could not send (HTTP 404)` | 資料表不存在：SQL 還沒執行；執行過的話，在 SQL Editor 跑 `notify pgrst, 'reload schema';`，並確認 Data API 的 Exposed schemas 有 `public` |
| `could not send (HTTP 400)` | 留言太長或含不允許的字元 |
| 留言送得出去，重新整理卻看不到 | 讀取的 select 權限沒設好，重新執行一次 SQL |
| 中文在 MYDOS 裡顯示成 `?` | 正常：MYDOS 的字型只有 ASCII，中文請看桌面的「留言簿」視窗 |

## 練習題

- [ ] `GUESTBOOK` 加上分頁：每 20 行停一下，按任意鍵繼續
- [ ] 讓網頁送出結果回傳給 MYDOS（例如再用序列埠送一行字回去），`SIGN` 就能顯示「已上線」或「失敗」
- [ ] 在 Supabase 加一個 `hidden` 欄位，被你標記的留言就不顯示（不用真的刪除）
- [ ] 進階：做 16×16 中文字型，讓 MYDOS 裡也能顯示中文留言

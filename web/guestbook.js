// MYDOS 線上留言簿（網頁端）
//
//   讀：開機前從 Supabase 抓最新留言，就地寫進磁碟映像裡預留的 C:\GUEST\BOOK.TXT
//   寫：MYDOS 的 SIGN 指令從序列埠送出 OSC 序列「ESC ] 7777 ; SIGN ; 名字 ; 留言 BEL」，
//       index.html 攔截後呼叫 Guestbook.sign() 存進 Supabase
//
// 安全：留言是別人寫的，絕對不能原樣塞進終端機（可能夾帶 ANSI 跳脫碼），
// 寫進 BOOK.TXT 前一律只留可印的 ASCII；網頁視窗用 textContent 顯示，不會被當成 HTML。

const Guestbook = (() => {
    const cfg = window.MYDOS_GUESTBOOK || {};
    const enabled = !!(cfg.url && cfg.anonKey);
    // 網址只要專案根目錄；就算多貼了 /rest/v1/ 也自動去掉
    const api = enabled ? cfg.url.trim().replace(/\/+$/, '').replace(/\/rest\/v1$/, '') + '/rest/v1/guestbook' : '';
    // 新版 publishable key（sb_publishable_...）只能放在 apikey 標頭；
    // 舊版 anon key 是 JWT（eyJ 開頭），才另外放進 Authorization
    const headers = !enabled ? {} : cfg.anonKey.startsWith('eyJ')
        ? { apikey: cfg.anonKey, Authorization: 'Bearer ' + cfg.anonKey }
        : { apikey: cfg.anonKey };
    const HEAD = 'MYDOS GUESTBOOK - type SIGN to leave a message\n' +
                 '==============================================\n';
    let entries = [];
    let lastSign = 0;

    async function load(limit = 200) {
        if (!enabled) { render(); return []; }
        const ctrl = new AbortController();
        const timer = setTimeout(() => ctrl.abort(), 6000);      // 資料庫太慢就不等，照常開機
        try {
            const r = await fetch(api + '?select=name,message,created_at&order=id.desc&limit=' + limit,
                                  { headers, signal: ctrl.signal });
            if (!r.ok) throw new Error('HTTP ' + r.status);
            entries = await r.json();
        } catch (e) {
            console.warn('Guestbook: could not load', e);
            entries = [];
        } finally {
            clearTimeout(timer);
        }
        render();
        return entries;
    }

    // MYDOS 的字型只有 ASCII：中文等字元換成 ?，控制字元一律拿掉
    function clean(s) { return String(s).replace(/[\x00-\x1f\x7f]/g, ''); }   // 網頁視窗：去掉控制字元
    function ascii(s) { return String(s).replace(/[^\x20-\x7e]/g, (c) => (c === '\n' || c === '\t') ? ' ' : '?'); }
    function stamp(iso) {
        const d = new Date(iso), p = (n) => String(n).padStart(2, '0');
        return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
    }

    // 在映像裡找到 BOOK.TXT 的預留區，就地覆寫（大小不變，剩下的填 ^Z）
    function patchDisk(img) {
        if (!enabled) return;
        const head = new TextEncoder().encode('MYDOS GUESTBOOK - type SIGN');
        const start = indexOf(img, head);
        if (start < 0) return;
        let end = start;
        while (end < img.length && img[end] !== 0x1a) end++;   // 原本的文字
        while (end < img.length && img[end] === 0x1a) end++;   // 預留的 ^Z
        const cap = end - start - 1;                           // 至少留一個 ^Z 當結尾

        let text = HEAD;
        if (!entries.length) text += '(No messages yet. Type SIGN to be the first!)\n\n';
        for (const e of entries) {
            const one = stamp(e.created_at) + '  ' + ascii(e.name) + '\n  ' + ascii(e.message) + '\n\n';
            if (text.length + one.length > cap) break;
            text += one;
        }
        const bytes = new TextEncoder().encode(text);
        img.fill(0x1a, start, end);
        img.set(bytes, start);
    }

    function indexOf(hay, needle) {
        outer: for (let i = 0; i <= hay.length - needle.length; i++) {
            if (hay[i] !== needle[0]) continue;
            for (let j = 1; j < needle.length; j++) if (hay[i + j] !== needle[j]) continue outer;
            return i;
        }
        return -1;
    }

    async function sign(name, message) {
        name = String(name).trim().slice(0, 20);
        message = String(message).trim().slice(0, 200);
        if (!name || !message) return { ok: false, text: 'Guestbook: name and message are required.' };
        if (!enabled) return { ok: false, text: 'Guestbook is not set up on this site yet — your message stays in this tab only.' };
        if (Date.now() - lastSign < 30000) return { ok: false, text: 'Guestbook: please wait 30 seconds between messages.' };
        try {
            const r = await fetch(api, {
                method: 'POST',
                headers: { ...headers, 'Content-Type': 'application/json', Prefer: 'return=minimal' },
                body: JSON.stringify({ name, message }),
            });
            if (!r.ok) throw new Error('HTTP ' + r.status);
            lastSign = Date.now();
            entries.unshift({ name, message, created_at: new Date().toISOString() });
            render();
            return { ok: true, text: 'Guestbook: thanks, ' + name + '! Your message is online.' };
        } catch (e) {
            return { ok: false, text: 'Guestbook: could not send (' + e.message + '). Saved in this tab only.' };
        }
    }

    // 桌面上的「留言簿」視窗
    function render() {
        const list = document.getElementById('gb-list');
        if (!list) return;
        list.textContent = '';
        if (!enabled) {
            list.textContent = '這個網站還沒有設定線上留言簿。（站長：請看 docs/guestbook-setup.md）';
            return;
        }
        if (!entries.length) { list.textContent = '還沒有留言，成為第一個吧！'; return; }
        for (const e of entries) {
            const div = document.createElement('div');
            div.className = 'gb-entry';
            const meta = document.createElement('div');
            meta.className = 'gb-meta';
            const who = document.createElement('b');
            who.textContent = clean(e.name);                            // textContent：不會被當成 HTML
            meta.append(who, ' · ' + stamp(e.created_at));
            const msg = document.createElement('div');
            msg.textContent = clean(e.message);
            div.append(meta, msg);
            list.append(div);
        }
    }

    return { enabled, load, patchDisk, sign, render };
})();

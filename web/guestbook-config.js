// 留言簿設定：填入你的 Supabase 專案資料（做法見 docs/guestbook-setup.md）。
// 這兩個值本來就是公開的（每個訪客的瀏覽器都看得到），
// 安全性靠的是資料庫的 Row Level Security 規則，不是把它們藏起來。
// 留空 = 停用線上留言簿（SIGN 只會存在訪客自己的分頁裡）。
window.MYDOS_GUESTBOOK = {
    url: 'https://tllbomswdrjfftekqifz.supabase.co/rest/v1/',        // 例如 'https://abcdefghijklmnop.supabase.co'
    anonKey: 'sb_publishable_aiBC63kzkLUoIyWtNE5lrw_fJeM-bJO',    // Project Settings → API Keys → Publishable key（sb_publishable_…）
};

// MYDOS 網頁版：設定 QEMU Wasm 怎麼開機
//
// 瀏覽器裡沒有真的硬碟，所以在 QEMU 啟動前（preRun），先把 mydos.iso 和
// disk.img 下載下來（傳輸時是 .gz，用瀏覽器內建的 DecompressionStream 解壓），
// 寫進 Emscripten 的記憶體檔案系統 /mydos/，
// 再用跟 `make run` 幾乎一樣的參數開機。
// 注意：C 槽的變更只存在記憶體裡，重新整理網頁就會回到初始狀態。

var Module = typeof Module !== 'undefined' ? Module : {};

Module['arguments'] = [
    '-nographic',                       // 沒有圖形畫面：COM1 接到網頁上的終端機
    '-M', 'pc', '-m', '256M',
    '-accel', 'tcg,tb-size=500',
    '-L', '/pack-rom/',                 // BIOS 和 VGA BIOS（load-rom.js 放進來的）
    '-nic', 'none',
    '-cdrom', '/mydos/mydos.iso',
    '-drive', 'file=/mydos/disk.img,format=raw,if=ide,index=0',
    '-boot', 'd',
    '-rtc', 'base=localtime',
];

Module['locateFile'] = function (path) {
    return new URL('qemu/' + path, document.baseURI).href;
};
Module['mainScriptUrlOrBlob'] = new URL('qemu/out.js', document.baseURI).href;

Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function (mod) {
    if (Module['ENVIRONMENT_IS_PTHREAD']) return;
    const files = ['mydos.iso', 'disk.img'];
    for (const name of files) {
        Module['addRunDependency']('mydos:' + name);
        fetch(name + '.gz')
            .then((r) => {
                if (!r.ok) throw new Error('HTTP ' + r.status);
                const unzipped = r.body.pipeThrough(new DecompressionStream('gzip'));
                return new Response(unzipped).arrayBuffer();
            })
            .then((buf) => {
                Module['FS_createPath']('/', 'mydos', true, true);
                Module['FS_createDataFile']('/mydos', name, new Uint8Array(buf), true, true, true);
                Module['removeRunDependency']('mydos:' + name);
            })
            .catch((e) => showStatus('Failed to load ' + name + ': ' + e.message, true));
    }
});

function showStatus(text, isError) {
    const el = document.getElementById('status');
    if (!el) return;
    el.textContent = text;
    el.className = isError ? 'error' : '';
    if (isError && typeof setLed === 'function') setLed('err');
}

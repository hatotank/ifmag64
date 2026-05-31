#ifndef IFMAG64_H
#define IFMAG64_H

#include <windows.h>

// ============================================================
// Susie 64bit Plug-in (SPH) API 型定義
// 仕様: https://toro.d.dooo.jp/dlsphapi.html
// ============================================================

// プログレスコールバック (32bit の long → 64bit の LONG_PTR)
typedef int (__stdcall *SUSIE_PROGRESS)(int nNum, int nDenom, LONG_PTR lData);

// 画像情報構造体 (64bit版: dummy[2] でアラインメント調整)
// NOTE: 32bit版と64bit版で hInfo のオフセットが異なるため注意
#pragma pack(push, 1)
struct PictureInfo {
    long    left;       // 展開位置 X
    long    top;        // 展開位置 Y
    long    width;      // 画像の幅 (pixel)
    long    height;     // 画像の高さ (pixel)
    WORD    x_density;  // 画素の水平密度
    WORD    y_density;  // 画素の垂直密度
    short   colorDepth; // 1画素当たりのビット数
#ifdef _WIN64
    char    dummy[2];   // アラインメント合わせ (64bit版のみ)
#endif
    HLOCAL  hInfo;      // テキスト情報ハンドル (未使用の場合 NULL)
};
#pragma pack(pop)

// ============================================================
// エラーコード (Susie Plug-in 共通)
// ============================================================
#define SPI_NO_FUNCTION      (-1)  // 機能未実装
#define SPI_ALL_RIGHT          0   // 正常終了
#define SPI_ABORT              1   // コールバックにより中止
#define SPI_NOT_SUPPORT        2   // 未知のフォーマット
#define SPI_OUT_OF_ORDER       3   // データが壊れている
#define SPI_NO_MEMORY          4   // メモリ確保失敗
#define SPI_MEMORY_ERROR       5   // メモリエラー (Lock 失敗など)
#define SPI_FILE_READ_ERROR    6   // ファイル読み込みエラー
#define SPI_WINDOW_ERROR       7   // (予約)
#define SPI_OTHER_ERROR        8   // 内部エラー
#define SPI_FILE_WRITE_ERROR   9   // ファイル書き込みエラー
#define SPI_END_OF_FILE       10   // EOF

// ============================================================
// MAG フォーマット定義
// 参考: Magbible.txt
// ============================================================

// ファイル先頭 8バイトの識別マジック ("MAKI02  ")
#define MAG_MAGIC           "MAKI02  "
#define MAG_MAGIC_LEN       8

// コメント終端 / ヘッダ開始マーカー
#define MAG_HEADER_MARK     0x1A

// IsSupported に渡すバッファサイズ (SPH仕様: 2KB)
#define MAG_HEADBUF_SIZE    2048

// スクリーンモードフラグ (ヘッダ +3 バイト目, $1Aの直後から数えて)
#define MAG_SCREEN_200LINE  0x01  // 200ライン (縦2:1 ドット比)
#define MAG_SCREEN_8COLOR   0x02  // 8色モード
#define MAG_SCREEN_DIGITAL  0x04  // デジタルパレット
#define MAG_SCREEN_256COLOR 0x80  // 256色モード (bit7)

// ============================================================
// MAG ヘッダ構造体 (32バイト)
//
// ファイルレイアウト:
//   [0..7]        : "MAKI02  " マジック
//   [8..N]        : コメント (可変長テキスト, $1A で終端)
//   [$1A位置]     : $1A マーカー (= hed_pos)
//   [hed_pos+1]   : MagHeader 先頭 ← この構造体
//   [hed_pos+33~] : パレットデータ (16色: 48バイト, 256色: 768バイト)
//   [hed_pos+1+offset_flag_a] : フラグAデータ
//   [hed_pos+1+offset_flag_b] : フラグBデータ
//   [hed_pos+1+offset_pixel]  : ピクセルデータ
//
// 各 offset_* は hed_pos+1 からの相対オフセット
// ============================================================
#pragma pack(push, 1)
struct MagHeader {
    BYTE  machine_code;     // +0  機械コード (PC98=$03, X68K=$00 等)
    BYTE  reserved1;        // +1  予約
    BYTE  machine_flags;    // +2  機械依存フラグ
    BYTE  screen_mode;      // +3  スクリーンモード (MAG_SCREEN_* 参照)
    WORD  start_x;          // +4  表示開始 X 座標
    WORD  start_y;          // +6  表示開始 Y 座標
    WORD  end_x;            // +8  表示終了 X 座標 (幅-1 を加算した値)
    WORD  end_y;            // +10 表示終了 Y 座標
    DWORD offset_flag_a;    // +12 フラグA データのオフセット (hed_pos+1 起点)
    DWORD offset_flag_b;    // +16 フラグB データのオフセット
    DWORD size_flag_b;      // +20 フラグB データのサイズ (バイト)
    DWORD offset_pixel;     // +24 ピクセルデータのオフセット
    DWORD size_pixel;       // +28 ピクセルデータのサイズ (バイト)
};
#pragma pack(pop)

// MagHeader は必ず 32バイトであること
static_assert(sizeof(MagHeader) == 32, "MagHeader size must be 32 bytes");

// ============================================================
// エクスポート関数プロトタイプ (extern "C" + __stdcall)
// ============================================================
extern "C" {
    // プラグイン情報取得 [必須]
    int __declspec(dllexport) __stdcall GetPluginInfo(
        int infono, LPSTR buf, int buflen);

    // フォーマット判定 [必須]
    // dw: ファイル先頭 2KB を格納したバッファへのポインタ
    int __declspec(dllexport) __stdcall IsSupported(
        LPCSTR filename, const void *dw);

    // 画像情報取得 [無API注意: 常に -1 を返すプラグインもある]
    int __declspec(dllexport) __stdcall GetPictureInfo(
        LPCSTR buf, LONG_PTR len, unsigned int flag,
        struct PictureInfo *lpInfo);

    // 画像展開 (メインデコード) [必須]
    int __declspec(dllexport) __stdcall GetPicture(
        LPCSTR buf, LONG_PTR len, unsigned int flag,
        HLOCAL *pHBInfo, HLOCAL *pHBm,
        SUSIE_PROGRESS lpProgressCallback, LONG_PTR lData);

    // プレビュー展開 [未実装可]
    int __declspec(dllexport) __stdcall GetPreview(
        LPCSTR buf, LONG_PTR len, unsigned int flag,
        HLOCAL *pHBInfo, HLOCAL *pHBm,
        SUSIE_PROGRESS lpProgressCallback, LONG_PTR lData);
}

#endif // IFMAG64_H

/*
 * ifmag64.cpp  -  MAG画像形式 Susie 64bit プラグイン (SPH)
 *
 * MAG フォーマット仕様:
 *  http://metanest.jp/mag/mag.xhtml
 *  Magbible.txt, magseigo.txt
 * SPH API 仕様:
 *  https://toro.d.dooo.jp/dlsphapi.html
 * Susie 32bit Plug-in 仕様 rev5
 *  https://www.digitalpad.co.jp/~takechin/ (Plug-in package ver0.08同梱)
 * 参考実装:
 *  https://www.asahi-net.or.jp/~kh4s-smz/spi/make_spi.html
 *  https://github.com/toroidj/iftwic/blob/master/IFTWIC.CPP
 *
 * ビルド: cl.exe /LD /O2 /EHsc ifmag64.cpp /Feifmag64.sph user32.lib
 */

#include "ifmag64.h"
#include <string.h>  // memcmp, memcpy, memset

// ============================================================
// プラグイン情報テーブル
// ============================================================
static const char * const s_pluginfo[] = {
    "00IN",                                // [0] Plug-in API バージョン (画像展開)
    "MAG image loader ver1.0 (c)hatotank", // [1] プラグイン名・バージョン
    "*.mag",                               // [2] 対応拡張子
    "MAG image (*.MAG)",                   // [3] ファイル形式名
};

// ============================================================
// DllMain
// ============================================================
BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    (void)hModule;
    (void)ul_reason_for_call;
    (void)lpReserved;
    return TRUE;
}

// ============================================================
// 内部ヘルパー: $1A マーカーを走査してヘッダ位置 (hed_pos) を返す
//
// ファイル構造:
//   [0..7] : "MAKI02  " マジック
//   [8..N] : コメントテキスト (可変長)
//   [N]    : $1A マーカー = hed_pos  ← この位置を返す
//   [N+1]  : MagHeader 先頭
//
// 戻り値: $1A の位置 (bytes from data head), 見つからない場合は -1
// ============================================================
static LONG_PTR FindMagHeaderPos(const unsigned char *data, SIZE_T datasize)
{
    // マジックチェック
    if (datasize < MAG_MAGIC_LEN)
        return -1;
    if (memcmp(data, MAG_MAGIC, MAG_MAGIC_LEN) != 0)
        return -1;

    // $1A を先頭から走査 (コメント領域の最短は ~31バイト程度)
    // 安全のため MAG_MAGIC_LEN から走査する
    for (SIZE_T i = MAG_MAGIC_LEN; i < datasize; ++i) {
        if (data[i] == MAG_HEADER_MARK) {
            // ヘッダ本体 (32バイト) + 最小パレット (48バイト) が収まるか確認
            if (i + 1 + sizeof(MagHeader) + 48 > datasize)
                return -1;
            return (LONG_PTR)i;
        }
    }
    return -1;
}

// ============================================================
// GetPluginInfo [必須]
// ============================================================
extern "C" int __stdcall GetPluginInfo(int infono, LPSTR buf, int buflen)
{
    int count = (int)(sizeof(s_pluginfo) / sizeof(s_pluginfo[0]));
    if (infono < 0 || infono >= count)
        return 0;

    lstrcpynA(buf, s_pluginfo[infono], buflen);
    return lstrlenA(buf);
}

// ============================================================
// IsSupported [必須]
// SPH仕様: dw はファイル先頭 2KB のバッファへのポインタ
// (ファイルハンドル渡しは64bit環境では使用禁止)
// ============================================================
extern "C" int __stdcall IsSupported(LPCSTR filename, const void *dw)
{
    (void)filename;  // ファイル名は使用しない (Susie 純正仕様に準拠)
    BYTE *header;

    if (dw == NULL)
        return 0;

    // iftwic (TORO氏) と同じ判定:
    // 上位32bitが非ゼロなら 64bit ヒープポインタ → バッファとして処理
    // 上位32bitがゼロなら ファイルハンドル or runsph の bad handle テスト (0x7f等) →
    //   そのままポインタとして memcmp するとアクセス違反になるため 0 を返す
    if ( (DWORD_PTR)dw & ~(DWORD_PTR)0xFFFFFFFF ) {
        header = (BYTE *)dw;
    } else {
        return 0;
    }

    // マジックバイト "MAKI02  " を確認
    if (memcmp(header, MAG_MAGIC, MAG_MAGIC_LEN) == 0)
        return 1;

    return 0;
}

// ============================================================
// GetPictureInfo [無API注意]
// ============================================================
extern "C" int __stdcall GetPictureInfo(
    LPCSTR buf, LONG_PTR len, unsigned int flag,
    struct PictureInfo *lpInfo)
{
    const unsigned char *data = NULL;
    unsigned char headbuf[MAG_HEADBUF_SIZE];
    HANDLE hf = INVALID_HANDLE_VALUE;

    if ((flag & 7) == 0) {
        // ファイル名渡し
        hf = CreateFileA(buf, GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf == INVALID_HANDLE_VALUE)
            return SPI_FILE_READ_ERROR;

        // オフセット位置にシーク (LONG_PTR = 64bit 対応)
        LARGE_INTEGER liOffset;
        liOffset.QuadPart = (LONGLONG)len;
        if (!SetFilePointerEx(hf, liOffset, NULL, FILE_BEGIN)) {
            CloseHandle(hf);
            return SPI_FILE_READ_ERROR;
        }

        DWORD readBytes = 0;
        if (!ReadFile(hf, headbuf, MAG_HEADBUF_SIZE, &readBytes, NULL)
            || readBytes < (DWORD)(sizeof(MagHeader) + 80)) {
            CloseHandle(hf);
            return SPI_NOT_SUPPORT;
        }
        CloseHandle(hf);
        data = headbuf;
    } else {
        // メモリ渡し
        if (len < (LONG_PTR)(sizeof(MagHeader) + 80))
            return SPI_NOT_SUPPORT;
        data = reinterpret_cast<const unsigned char *>(buf);
    }

    LONG_PTR hed_pos = FindMagHeaderPos(data, MAG_HEADBUF_SIZE);
    if (hed_pos < 0)
        return SPI_NOT_SUPPORT;

    const MagHeader *hdr =
        reinterpret_cast<const MagHeader *>(data + hed_pos + 1);

    int img_width  = (int)(hdr->end_x - hdr->start_x + 1);
    int img_height = (int)(hdr->end_y - hdr->start_y + 1);

    if (img_width <= 0 || img_height <= 0)
        return SPI_OUT_OF_ORDER;

    lpInfo->left       = 0;
    lpInfo->top        = 0;
    lpInfo->width      = (long)img_width;
    lpInfo->height     = (long)img_height;
    lpInfo->x_density  = 0;
    lpInfo->y_density  = 0;
    lpInfo->colorDepth = (hdr->screen_mode & MAG_SCREEN_256COLOR) ? 8 : 4;
    lpInfo->hInfo      = NULL;

    // コメントデータの取得 (data[MAG_MAGIC_LEN] .. data[hed_pos-1])
    // Magbible.txt: 「テキストデータの最後に EOF($1A) を入れておく」
    SIZE_T comment_len = (SIZE_T)hed_pos - MAG_MAGIC_LEN;
    lpInfo->hInfo = NULL;
    if (comment_len > 0) {
        HLOCAL hComment = LocalAlloc(LMEM_MOVEABLE, comment_len + 1);
        if (hComment != NULL) {
            char *p = static_cast<char *>(LocalLock(hComment));
            if (p != NULL) {
                memcpy(p, data + MAG_MAGIC_LEN, comment_len);
                p[comment_len] = '\0';
                LocalUnlock(hComment);
                lpInfo->hInfo = hComment;
            } else {
                LocalFree(hComment);
            }
        }
    }

    return SPI_ALL_RIGHT;
}

// ============================================================
// 内部関数: MAGデータをデコードして BITMAPINFO + ピクセルデータを返す
//
// 引数:
//   data     : ファイル全体のメモリイメージ (先頭から)
//   datasize : data のバイト数
//   pHBInfo  : BITMAPINFO 構造体を格納した HLOCAL を受け取るポインタ
//   pHBm     : ピクセルデータを格納した HLOCAL を受け取るポインタ
//
// MAG デコードアルゴリズム:
//   1. フラグA (1bit/ユニット行) + フラグB (バイト) から フラグデータ を復元
//      - 各行は前行との XOR 差分で圧縮されている → XOR で元に戻す
//   2. フラグデータ (4bit/ユニット) に従いピクセルデータを展開
//      - フラグ=0: ピクセルデータから新規読み込み
//      - フラグ1-15: (disx[f], disy[f]) だけ遡った位置からコピー
//   3. 展開結果をボトムアップ DIB 形式に変換して返す
// ============================================================
static int DecodeMag(
    const unsigned char *data, SIZE_T datasize,
    HLOCAL *pHBInfo, HLOCAL *pHBm,
    SUSIE_PROGRESS lpProgressCallback, LONG_PTR lData)
{
    *pHBInfo = NULL;
    *pHBm    = NULL;

    // --- ヘッダ解析 ---
    LONG_PTR hed_pos = FindMagHeaderPos(data, datasize);
    if (hed_pos < 0)
        return SPI_NOT_SUPPORT;

    const unsigned char *hdr_base = data + hed_pos + 1;
    const MagHeader     *hdr      =
        reinterpret_cast<const MagHeader *>(hdr_base);

    bool is256color = (hdr->screen_mode & MAG_SCREEN_256COLOR) != 0;
    int  pal_colors = is256color ? 256 : 16;
    int  bpp        = is256color ? 8 : 4;   // DIB ビット深度
    // pdot_num: 1フラグユニットが何ドットをカバーするか
    //   16色: 4dots/unit (1 WORD = 4 nibbles = 4 palette indices)
    //   256色: 2dots/unit (1 WORD = 2 bytes = 2 palette indices)
    int  pdot_num   = is256color ? 2 : 4;

    int img_width  = (int)(hdr->end_x - hdr->start_x + 1);
    int img_height = (int)(hdr->end_y - hdr->start_y + 1);

    if (img_width <= 0 || img_height <= 0)
        return SPI_OUT_OF_ORDER;

    // --- 各エリアの絶対オフセット計算 ---
    // offset_* は hdr_base (= data + hed_pos + 1) からの相対オフセット
    SIZE_T fa_abs  = (SIZE_T)(hed_pos + 1) + hdr->offset_flag_a;
    SIZE_T fb_abs  = (SIZE_T)(hed_pos + 1) + hdr->offset_flag_b;
    SIZE_T pix_abs = (SIZE_T)(hed_pos + 1) + hdr->offset_pixel;

    if (fa_abs  >= datasize || fb_abs  >= datasize ||
        pix_abs >= datasize)
        return SPI_OUT_OF_ORDER;

    // フラグB と ピクセルデータのサイズ範囲チェック
    if (fb_abs  + hdr->size_flag_b > datasize ||
        pix_abs + hdr->size_pixel  > datasize)
        return SPI_OUT_OF_ORDER;

    const unsigned char  *fa_ptr  = data + fa_abs;
    const unsigned char  *fb_ptr  = data + fb_abs;
    const unsigned short *pix_src =
        reinterpret_cast<const unsigned short *>(data + pix_abs);

    // パレットデータ: hdr_base + sizeof(MagHeader) の位置
    const unsigned char *pal_raw = hdr_base + sizeof(MagHeader);
    if ((SIZE_T)((pal_raw - data) + (SIZE_T)(pal_colors * 3)) > datasize)
        return SPI_OUT_OF_ORDER;

    // --- フラグバッファの計算 ---
    // tmsizx: pdot_num の倍数に切り上げた画像幅
    int tmsizx   = (img_width + pdot_num - 1) / pdot_num * pdot_num;
    // px_sizx: 水平方向のユニット数
    int px_sizx  = tmsizx / pdot_num;
    // f_byte_x: 1行あたりのフラグバイト数 (1バイトに2ユニット分の4bitフラグ)
    int f_byte_x = px_sizx / 2;

    if (f_byte_x == 0)
        return SPI_OUT_OF_ORDER;

    SIZE_T f_byte_total = (SIZE_T)f_byte_x * img_height;

    // フラグA のサイズ (ビット配列 → バイト配列に切り上げ)
    SIZE_T fa_size = (f_byte_total + 7) / 8 + 1;
    if (fa_abs + fa_size > datasize)
        return SPI_OUT_OF_ORDER;

    // コールバック: 0/3
    if (lpProgressCallback != NULL)
        if (lpProgressCallback(0, 3, lData)) return SPI_ABORT;

    // ============================================================
    // STEP 1: フラグデータ (flg_dat) の復元
    //
    // フラグA/B の格納形式:
    //   - フラグA: ビット配列 (MSB first), 1bit/ユニット行エントリ
    //     bit=1 → フラグBから1バイト読む (XOR差分が非ゼロ)
    //     bit=0 → 差分は0 (前行と同じ値)
    //   - フラグB: 差分が非ゼロの場合のみ格納されたバイト列
    //
    // 復元後の flg_dat[y * f_byte_x + bx]:
    //   上位ニブル → ユニット (bx*2)   のフラグ値 (0-15)
    //   下位ニブル → ユニット (bx*2+1) のフラグ値
    // ============================================================
    unsigned char *flg_dat = static_cast<unsigned char *>(
        LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, f_byte_total));
    if (flg_dat == NULL)
        return SPI_NO_MEMORY;

    {
        const unsigned char *fa = fa_ptr;
        const unsigned char *fb = fb_ptr;
        int fa_bit = 7;  // 現在読み中のビット位置 (7=MSB, 0=LSB)

        unsigned char *cur_row  = flg_dat;
        unsigned char *prev_row = NULL;

        for (int y = 0; y < img_height; ++y, cur_row += f_byte_x) {
            for (int bx = 0; bx < f_byte_x; ++bx) {
                // フラグA から 1ビット読む (MSB first)
                unsigned char fa_bit_val = (*fa >> fa_bit) & 1;
                if (--fa_bit < 0) { fa_bit = 7; ++fa; }

                // フラグB から XOR バイトを取得 (bit=0 なら差分ゼロ)
                unsigned char xor_byte = fa_bit_val ? *fb++ : 0;

                // XOR 展開: y=0 行目はそのまま, y>0 は前行と XOR
                cur_row[bx] = (prev_row == NULL) ? xor_byte
                                                 : (xor_byte ^ prev_row[bx]);
            }
            prev_row = cur_row;
        }
    }

    // コールバック: 1/3
    if (lpProgressCallback != NULL)
        if (lpProgressCallback(1, 3, lData)) {
            LocalFree(flg_dat);
            return SPI_ABORT;
        }

    // ============================================================
    // STEP 2: ピクセルデータ展開 → 作業バッファ (work_buf) へ
    //
    // work_buf[y * px_sizx + x]: 各ユニットを WORD (16bit) で保持
    //   16色:  1 WORD = 4 nibbles = 4 pixels (nibble pack: byte0=dot0,dot1 / byte1=dot2,dot3)
    //   256色: 1 WORD = 2 bytes   = 2 pixels (低バイト=左dot, 高バイト=右dot)
    //
    // フラグ値→相対コピー元オフセット (Magbible.txt 参照):
    //   flag   : 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    //   disx[] : 0  1  2  4  0  1  0  1  2  0  1  2  0  1  2  0
    //   disy[] : 0  0  0  0  1  1  2  2  2  4  4  4  8  8  8 16
    //   (flag=0 は新規ピクセル, 1-15 は (x-disx, y-disy) からコピー)
    // ============================================================
    static const int disx[16] = {0, 1, 2, 4, 0, 1, 0, 1, 2, 0, 1, 2,  0, 1, 2,  0};
    static const int disy[16] = {0, 0, 0, 0, 1, 1, 2, 2, 2, 4, 4, 4,  8, 8, 8, 16};

    SIZE_T wbuf_size = (SIZE_T)px_sizx * img_height * sizeof(unsigned short);
    unsigned short *work_buf = static_cast<unsigned short *>(
        LocalAlloc(LMEM_FIXED, wbuf_size));
    if (work_buf == NULL) {
        LocalFree(flg_dat);
        return SPI_NO_MEMORY;
    }

    for (int y = 0; y < img_height; ++y) {
        unsigned short       *row_ptr = work_buf + (SIZE_T)y * px_sizx;
        const unsigned char  *flg_row = flg_dat  + (SIZE_T)y * f_byte_x;

        for (int x = 0; x < px_sizx; ++x) {
            // フラグニブル取得 (上位=偶数ユニット, 下位=奇数ユニット)
            unsigned char flag_nibble;
            if ((x & 1) == 0)
                flag_nibble = (flg_row[x >> 1] >> 4) & 0x0F;
            else
                flag_nibble =  flg_row[x >> 1]        & 0x0F;

            if (flag_nibble == 0) {
                // 新規ピクセル: ピクセルデータから読み込み
                row_ptr[x] = *pix_src++;
            } else {
                // 相対コピー
                int sx = x - disx[flag_nibble];
                int sy = y - disy[flag_nibble];
                // NOTE: 仕様上 sx<0 / sy<0 は発生しないはずだが安全のため
                if (sx >= 0 && sy >= 0) {
                    row_ptr[x] = work_buf[(SIZE_T)sy * px_sizx + sx];
                } else {
                    row_ptr[x] = 0;
                }
            }
        }
    }

    LocalFree(flg_dat);
    flg_dat = NULL;

    // コールバック: 2/3
    if (lpProgressCallback != NULL)
        if (lpProgressCallback(2, 3, lData)) {
            LocalFree(work_buf);
            return SPI_ABORT;
        }

    // ============================================================
    // STEP 3: DIB 出力 (ボトムアップ DIB)
    //
    // pHBInfo: BITMAPINFOHEADER + RGBQUAD パレット
    // pHBm   : ビットマップデータ (ボトムアップ, 4バイトアライン)
    // ============================================================

    // BITMAPINFO サイズ = ヘッダ + パレット
    DWORD infosize   = sizeof(BITMAPINFOHEADER) + (DWORD)(pal_colors * sizeof(RGBQUAD));
    // DIB 1行バイト数 (DWORD アライン)
    int dib_linesize = ((img_width * bpp + 31) / 32) * 4;
    SIZE_T imgsize   = (SIZE_T)dib_linesize * img_height;

    *pHBInfo = LocalAlloc(LMEM_MOVEABLE, infosize);
    *pHBm    = LocalAlloc(LMEM_MOVEABLE, imgsize);
    if (*pHBInfo == NULL || *pHBm == NULL) {
        if (*pHBInfo) { LocalFree(*pHBInfo); *pHBInfo = NULL; }
        if (*pHBm)    { LocalFree(*pHBm);    *pHBm    = NULL; }
        LocalFree(work_buf);
        return SPI_NO_MEMORY;
    }

    BITMAPINFO    *bmi = static_cast<BITMAPINFO *>   (LocalLock(*pHBInfo));
    unsigned char *dib = static_cast<unsigned char *>(LocalLock(*pHBm));
    if (bmi == NULL || dib == NULL) {
        if (bmi) LocalUnlock(*pHBInfo);
        if (dib) LocalUnlock(*pHBm);
        LocalFree(*pHBInfo); *pHBInfo = NULL;
        LocalFree(*pHBm);    *pHBm    = NULL;
        LocalFree(work_buf);
        return SPI_MEMORY_ERROR;
    }

    // BITMAPINFOHEADER 設定
    bmi->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth         = img_width;
    bmi->bmiHeader.biHeight        = img_height;  // 正値 = ボトムアップ DIB
    bmi->bmiHeader.biPlanes        = 1;
    bmi->bmiHeader.biBitCount      = (WORD)bpp;
    bmi->bmiHeader.biCompression   = BI_RGB;
    bmi->bmiHeader.biSizeImage     = 0;
    bmi->bmiHeader.biXPelsPerMeter = 0;
    bmi->bmiHeader.biYPelsPerMeter = 0;
    bmi->bmiHeader.biClrUsed       = (DWORD)pal_colors;
    bmi->bmiHeader.biClrImportant  = 0;

    // パレット変換
    // MAG パレット形式: G, R, B 各1バイト (Magbible.txt 参照)
    RGBQUAD *dib_pal = bmi->bmiColors;
    //bool is_digital = (hdr->screen_mode & MAG_SCREEN_DIGITAL) != 0;
    for (int i = 0; i < pal_colors; ++i) {
        BYTE g = pal_raw[i * 3 + 0]; // G
        BYTE r = pal_raw[i * 3 + 1]; // R
        BYTE b = pal_raw[i * 3 + 2]; // B

        // アナログパレットの場合、16色は 4bit 値を 8bit に拡張して使用
        // 検証→拡張すると色がおかしいので保留、拡張なしで見た目上問題ないのでそのまま使用
        //if (!is256color && !is_digital) {
        //    g = (g << 4) | g;
        //    r = (r << 4) | r;
        //    b = (b << 4) | b;
        //}

        dib_pal[i].rgbGreen    = g;
        dib_pal[i].rgbRed      = r;
        dib_pal[i].rgbBlue     = b;
        dib_pal[i].rgbReserved = 0;
    }

    // ピクセルデータ: 上から下 (work_buf) → 下から上 (DIB ボトムアップ)
    // 各 WORD の低バイト = 左側ドット(群), 高バイト = 右側ドット(群)
    //   16色:  低バイト = dot0,dot1 packed nibbles / 高バイト = dot2,dot3
    //   256色: 低バイト = dot0 (8bit index) / 高バイト = dot1
    // DIB の 4bit/8bit 配置と字致するため、バイト順を保ったまま書き込む
    memset(dib, 0, imgsize);
    for (int y = 0; y < img_height; ++y) {
        // ボトムアップ: 上から y 行目 → DIB の (height-1-y) 行目
        unsigned char        *dib_row = dib + (SIZE_T)(img_height - 1 - y) * dib_linesize;
        const unsigned short *src_row = work_buf + (SIZE_T)y * px_sizx;

        for (int ux = 0; ux < px_sizx; ++ux) {
            int dib_byte = ux * 2;
            if (dib_byte + 1 >= dib_linesize) break;  // 行末パディング境界
            unsigned short w = src_row[ux];
            dib_row[dib_byte    ] = (unsigned char)( w       & 0xFF);  // 低バイト (左ドット群)
            dib_row[dib_byte + 1] = (unsigned char)((w >> 8) & 0xFF);  // 高バイト (右ドット群)
        }
    }

    LocalUnlock(*pHBInfo);
    LocalUnlock(*pHBm);
    LocalFree(work_buf);

    // コールバック: 3/3
    if (lpProgressCallback != NULL)
        lpProgressCallback(3, 3, lData);

    return SPI_ALL_RIGHT;
}

// ============================================================
// GetPicture [必須]
// ============================================================
extern "C" int __stdcall GetPicture(
    LPCSTR buf, LONG_PTR len, unsigned int flag,
    HLOCAL *pHBInfo, HLOCAL *pHBm,
    SUSIE_PROGRESS lpProgressCallback, LONG_PTR lData)
{
    int ret = SPI_OTHER_ERROR;
    unsigned char *filedata = NULL;
    SIZE_T datasize = 0;
    bool   owned    = false;  // filedata を自前でアロケートしたか

    if ((flag & 7) == 0) {
        // ファイル名渡し: ファイル全体を読み込む
        HANDLE hf = CreateFileA(buf, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf == INVALID_HANDLE_VALUE)
            return SPI_FILE_READ_ERROR;

        LARGE_INTEGER filesz;
        if (!GetFileSizeEx(hf, &filesz) || filesz.QuadPart <= (LONGLONG)len) {
            CloseHandle(hf);
            return SPI_FILE_READ_ERROR;
        }

        datasize = (SIZE_T)(filesz.QuadPart - (LONGLONG)len);

        LARGE_INTEGER liOffset;
        liOffset.QuadPart = (LONGLONG)len;
        if (!SetFilePointerEx(hf, liOffset, NULL, FILE_BEGIN)) {
            CloseHandle(hf);
            return SPI_FILE_READ_ERROR;
        }

        filedata = static_cast<unsigned char *>(LocalAlloc(LMEM_FIXED, datasize));
        if (filedata == NULL) {
            CloseHandle(hf);
            return SPI_NO_MEMORY;
        }

        // NOTE: datasize が 4GB を超えるファイルは非対応 (MAG画像では実用上問題なし)
        DWORD readBytes = 0;
        if (!ReadFile(hf, filedata, (DWORD)datasize, &readBytes, NULL)
            || readBytes != (DWORD)datasize) {
            CloseHandle(hf);
            LocalFree(filedata);
            return SPI_FILE_READ_ERROR;
        }
        CloseHandle(hf);
        owned = true;
    } else {
        // メモリ渡し: バッファをそのまま使用
        filedata = const_cast<unsigned char *>(
                       reinterpret_cast<const unsigned char *>(buf));
        datasize = (SIZE_T)len;
    }

    // フォーマット確認してからデコード
    if (!IsSupported(buf, filedata)) {
        ret = SPI_NOT_SUPPORT;
    } else {
        ret = DecodeMag(filedata, datasize,
                        pHBInfo, pHBm,
                        lpProgressCallback, lData);
    }

    if (owned)
        LocalFree(filedata);

    return ret;
}

// ============================================================
// GetPreview [未実装可]
// アプリケーション側は -1 が返ったとき GetPicture にフォールバックする
// ============================================================
extern "C" int __stdcall GetPreview(
    LPCSTR buf, LONG_PTR len, unsigned int flag,
    HLOCAL *pHBInfo, HLOCAL *pHBm,
    SUSIE_PROGRESS lpProgressCallback, LONG_PTR lData)
{
    (void)buf; (void)len; (void)flag;
    (void)pHBInfo; (void)pHBm;
    (void)lpProgressCallback; (void)lData;
    return SPI_NO_FUNCTION;
}

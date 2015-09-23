﻿//  -----------------------------------------------------------------------------------------
//    拡張 x264 出力(GUI) Ex  v1.xx by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#include <Windows.h>
#include <process.h>
#pragma comment(lib, "winmm.lib")
#include "auo.h"
#include "auo_version.h"
#include "auo_system.h"
#include "auo_audio.h"
#include "auo_frm.h"

typedef struct {
    CONF_GUIEX *_conf;
    const OUTPUT_INFO *_oip;
    PRM_ENC *_pe;
    const SYSTEM_DATA *_sys_dat;
} AUDIO_OUTPUT_PRM;

static inline void if_valid_close_handle(HANDLE *p_hnd) {
    if (*p_hnd) {
        CloseHandle(*p_hnd);
        *p_hnd = NULL;
    }
}

//音声並列処理スレッド用関数
static unsigned __stdcall audio_output_parallel_func(void *prm) {
    AUDIO_OUTPUT_PRM *aud_prm = (AUDIO_OUTPUT_PRM *)prm;
    CONF_GUIEX *conf = aud_prm->_conf;
    const OUTPUT_INFO *oip = aud_prm->_oip;
    PRM_ENC *pe = aud_prm->_pe;
    const SYSTEM_DATA *sys_dat = aud_prm->_sys_dat;
    free(prm); //audio_output_parallel関数内で確保したものをここで解放

    //_endthreadexは明示的なCloseHandleが必要 (exit_audio_parallel_control内で実行)
    _endthreadex(audio_output(conf, oip, pe, sys_dat));
    return 0;
}

//音声並列処理を開始する
AUO_RESULT audio_output_parallel(CONF_GUIEX *conf, const OUTPUT_INFO *oip, PRM_ENC *pe, const SYSTEM_DATA *sys_dat) {
    AUO_RESULT ret = AUO_RESULT_SUCCESS;
    //音声エンコードの必要がなければ終了
    if (!(oip->flag & OUTPUT_INFO_FLAG_AUDIO))
        return ret;
    AUDIO_OUTPUT_PRM *parameters = (AUDIO_OUTPUT_PRM *)malloc(sizeof(AUDIO_OUTPUT_PRM)); //スレッド関数(audio_output_parallel_func)内で解放
    if (parameters == NULL) return AUO_RESULT_ERROR;
    parameters->_conf = conf;
    parameters->_oip = oip;
    parameters->_pe = pe;
    parameters->_sys_dat = sys_dat;

    ZeroMemory(&pe->aud_parallel, sizeof(pe->aud_parallel));
    if        (NULL == (pe->aud_parallel.he_aud_start = CreateEvent(NULL, FALSE, FALSE, NULL))) {
        ret = AUO_RESULT_ERROR;
    } else if (NULL == (pe->aud_parallel.he_vid_start = CreateEvent(NULL, FALSE, FALSE, NULL))) {
        ret = AUO_RESULT_ERROR;
    } else if (NULL == (pe->aud_parallel.th_aud = (HANDLE)_beginthreadex(NULL, 0, audio_output_parallel_func, (void *)parameters, 0, NULL))) {
        ret = AUO_RESULT_ERROR;
    }

    if (ret == AUO_RESULT_ERROR) {
        if_valid_close_handle(&(pe->aud_parallel.he_aud_start));
        if_valid_close_handle(&(pe->aud_parallel.he_vid_start));
    }
    return ret;
}

//並列処理制御用のイベントをすべて解放する
//映像・音声どちらかのAviutlからのデータ取得が必要なくなった時点で呼ぶ
//呼び出しは映像・音声スレッドどちらでもよい
//この関数が呼ばれたあとは、映像・音声どちらも自由に動くようにする
void release_audio_parallel_events(PRM_ENC *pe) {
    if (pe->aud_parallel.he_aud_start) {
        //この関数が同時に呼ばれた場合のことを考え、InterlockedExchangePointerを使用してHANDLEを処理する
        HANDLE he_aud_start_copy = InterlockedExchangePointer(&(pe->aud_parallel.he_aud_start), NULL);
        SetEvent(he_aud_start_copy); //もし止まっていたら動かしてやる
        CloseHandle(he_aud_start_copy);
    }
    if (pe->aud_parallel.he_vid_start) {
        //この関数が同時に呼ばれた場合のことを考え、InterlockedExchangePointerを使用してHANDLEを処理する
        HANDLE he_vid_start_copy = InterlockedExchangePointer(&(pe->aud_parallel.he_vid_start), NULL);
        SetEvent(he_vid_start_copy); //もし止まっていたら動かしてやる
        CloseHandle(he_vid_start_copy);
    }
}

// Copyright 2012 David Turnbull AE9RB
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <peaberry.h>

uint8 Mic_Buff_Chan, Mic_Buff_TD[DMA_AUDIO_BUFS];
volatile uint8 Mic_Buff[USB_AUDIO_BUFS][MIC_BUF_SIZE], Mic_DMA_Buf;

CY_ISR(Mic_DMA_done) {
    uint8 td, bufnum, i;
    uint16 b, *buf;
    
    buf = (uint16*)Mic_Buff[Mic_DMA_Buf/DMA_USB_RATIO];
    buf += MIC_BUF_SIZE / (DMA_USB_RATIO*2) * (Mic_DMA_Buf & (DMA_USB_RATIO-1));
    for (i = MIC_BUF_SIZE / (DMA_USB_RATIO*2); i; ) {
        b = buf[--i];
        // DelSig will overflow (see datasheet)
        if (b & 0xF000) b = 2047;
        // changed to signed
        else b -= 2048;
        // rotate into USB bit order
        buf[i] = (b << 12) | (b >> 4);
    }

    td = DMAC_CH[Mic_Buff_Chan].basic_status[1] & 0x7Fu;
    bufnum = Mic_DMA_Buf + 1;
    if (bufnum >= DMA_AUDIO_BUFS) bufnum = 0;
    if (td != Mic_Buff_TD[bufnum]) {
        // resync, should only happen when debugging
        for (bufnum = 0; bufnum < DMA_AUDIO_BUFS; bufnum++) {
            if (td == Mic_Buff_TD[bufnum]) break;
        }
    }
    Mic_DMA_Buf = bufnum;
}

void Mic_Start(void)
{
    uint8 i, j, n;

    Mic_Buff_Chan = Mic_Buff_DmaInitialize(2, 1, HI16(CYDEV_PERIPH_BASE), HI16(CYDEV_SRAM_BASE));
    for (i=0; i < DMA_AUDIO_BUFS; i++) Mic_Buff_TD[i] = CyDmaTdAllocate();
    for (i=0; i < DMA_AUDIO_BUFS; ) {
        for (j=0; j < DMA_USB_RATIO; j++) {
             n = i + 1;
            if (n >= DMA_AUDIO_BUFS) n=0;
            CyDmaTdSetConfiguration(Mic_Buff_TD[i], MIC_BUF_SIZE/DMA_USB_RATIO, Mic_Buff_TD[n], TD_SWAP_EN | TD_INC_DST_ADR | Mic_Buff__TD_TERMOUT_EN );    
            CyDmaTdSetAddress(Mic_Buff_TD[i], LO16(&Microphone_DEC_SAMP_PTR), LO16(Mic_Buff[i/DMA_USB_RATIO] + MIC_BUF_SIZE/DMA_USB_RATIO * j));
            i++;
        }
    }
    CyDmaChSetInitialTd(Mic_Buff_Chan, Mic_Buff_TD[0]);
    
    Mic_DMA_Buf = 0;
    Mic_done_isr_Start();
    Mic_done_isr_SetVector(Mic_DMA_done);

    CyDmaChEnable(Mic_Buff_Chan, 1u);
    
    Microphone_Start();
    Microphone_StartConvert();
}

uint8* Mic_Buf(uint8* reset) {
    static uint8 use = 0;
    static int8 distance = 0;
    uint8 dma;
    dma = Mic_DMA_Buf;
    if (*reset) {
        use = dma/DMA_USB_RATIO;
        *reset = 0;
    }
    USBAudio_SyncBufs(dma, &use, &distance);
    return Mic_Buff[use];
}

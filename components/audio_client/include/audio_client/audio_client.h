
#pragma once

#include "driver/gpio.h"
#include "driver/i2s.h"

#include <cstdlib>
#include <stdint.h>
#include <string>

#include "audio_client/codec/g711.h"
#include "audio_client/rtp.h"

#include <algorithm>
#include <deque>
#include <time.h>

// debug cout
#include <iomanip>
#include <iostream>

#define AUDIO_BUFFER_LEN 160
#define VOIP_BUFFER_LEN 5

struct rtpBuffor {
  int32_t payload[AUDIO_BUFFER_LEN];
  uint32_t timestamp;

  bool operator<(rtpBuffor const &other) { return timestamp < other.timestamp; }
};

// buffers initialize
uint8_t transmitBuffer[AUDIO_BUFFER_LEN];
int32_t TXaudioBuffer[AUDIO_BUFFER_LEN];
std::deque<rtpBuffor> RXaudioBuffer;

// buffers status
bool RXbufferRendered = false;
bool TXbufferRendered = false;

// i2s status
bool i2sInitialized = false;
bool i2sPaused = false;

// RTP Payload Types audio codec
int audio_codec = -1; // should be 0 or 8 (PCMU, PCMA; first given in SDP header
                      // SHOULD be used)

// audio channel duplicator
int32_t chcopy(int16_t samp) {
  uint32_t sample = 0x00008000uL;
  uint16_t ua = (uint16_t)samp;
  uint16_t ub = (uint16_t)samp;
  sample = (((uint32_t)ua) << 16) | ((uint32_t)ub);
  return sample;
}

// audio config
#define SAMPLE_RATE 8000
#define BITS_PER_SAMPLE 16
#define BUFF_COU 8
#define BUFF_LEN 64
#define CHANNELS 1
#define MIC I2S_NUM_0
#define SPK I2S_NUM_0 // ESP32-C6 only has I2S_NUM_0, using duplex mode

// do not wait longer than the duration of the buffer
uint8_t buf_bytes_per_sample = (BITS_PER_SAMPLE / 8);
uint32_t num_samples = BUFF_LEN / buf_bytes_per_sample / CHANNELS;
TickType_t max_wait_spk = SAMPLE_RATE / num_samples / 4;
TickType_t max_wait_mic = SAMPLE_RATE / num_samples;

void i2s_pause() {
  if (i2sInitialized && !i2sPaused) {
    i2s_stop(SPK);
    i2s_stop(MIC);
    i2sPaused = true;
  }
}

void i2s_resume() {
  if (i2sInitialized && i2sPaused) {
    i2s_start(SPK);
    i2s_start(MIC);
    i2sPaused = false;
  }
}

// i2s init
void i2s_init() {
  if (!i2sInitialized) {
    // ESP32-C6 only has I2S_NUM_0, so we use it in duplex mode (RX + TX)
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX |
                                   I2S_MODE_TX); // Duplex mode
    i2s_config.sample_rate = SAMPLE_RATE;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; // 2-channels
    i2s_config.communication_format =
        (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    i2s_config.dma_buf_count = BUFF_COU * 2;
    i2s_config.dma_buf_len = BUFF_LEN;
    i2s_config.use_apll = false;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1; // Interrupt level 1

    i2s_pin_config_t pin_config = {
        .bck_io_num = GPIO_NUM_12,
        .ws_io_num = GPIO_NUM_15,
        .data_out_num = GPIO_NUM_2, // Speaker output
        .data_in_num = GPIO_NUM_13  // Microphone input
    };

    esp_err_t err1 = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    esp_err_t err2 = i2s_set_pin(I2S_NUM_0, &pin_config);

    if (err1 == ESP_OK && err2 == ESP_OK) {
      ESP_LOGI("audio_client", "i2s_init() - successful (duplex mode)");
      i2sInitialized = true;
    } else {
      ESP_LOGW("audio_client", "i2s_init() - error");
      // dsp_error_i2s();
    }
    i2s_pause();
  }
}

void audioTX(LwipUdpClient *socket) {
  size_t bytes_read = 0;
  i2s_read(I2S_NUM_0, &TXaudioBuffer, sizeof(TXaudioBuffer), &bytes_read,
           max_wait_mic);

  if (audio_codec == 0) {
    for (int i = 0; i < AUDIO_BUFFER_LEN; i++) {
      transmitBuffer[i] = linear2ulaw((int16_t)(TXaudioBuffer[i] >> 16));
    }
    TXbufferRendered = true;
  } else if (audio_codec == 8) {
    for (int i = 0; i < AUDIO_BUFFER_LEN; i++) {
      // debug
      //  std::cout << std::hex << TXaudioBuffer[i] << " - " << std::hex <<
      //  (int16_t)(TXaudioBuffer[i] >> 15) << std::endl; std::cout << std::hex
      //  << TXaudioBuffer[i] << " - " << std::hex << (int16_t)(TXaudioBuffer[i]
      //  >> 16) << std::endl; std::cout << std::hex << TXaudioBuffer[i] << " -
      //  " << std::hex << (int16_t)(TXaudioBuffer[i] >> 17) << std::endl;
      //  std::cout << std::hex << TXaudioBuffer[i] << " - " << std::hex <<
      //  (int16_t)(TXaudioBuffer[i] >> 18) << std::endl; std::cout << std::hex
      //  << TXaudioBuffer[i] << " - " << std::hex << (int16_t)(TXaudioBuffer[i]
      //  >> 19) << std::endl; std::cout << std::hex << TXaudioBuffer[i] << " -
      //  " << std::hex << (int16_t)(TXaudioBuffer[i] >> 20) << std::endl;
      //  std::cout << std::endl << std::endl;
      transmitBuffer[i] = linear2alaw(TXaudioBuffer[i] >> 16);
    }
    TXbufferRendered = true;
  }

  if (TXbufferRendered) {

    TxBufferT &tx_buffer = socket->get_new_tx_buf();

    // ver = 128; stałe
    pt = audio_codec;
    if (seq == 65535)
      seq = 256;
    else
      seq++;
    if ((ts + 160) < 4294967295)
      ts += 160;
    else
      ts = 65536;

    tx_buffer << ver;
    tx_buffer << pt;
    tx_buffer << (uint8_t)(seq >> 8) << (uint8_t)(seq & 0xFF);
    tx_buffer << (uint8_t)((ts & 0xFF000000) >> 24)
              << (uint8_t)((ts & 0x00FF0000) >> 16)
              << (uint8_t)((ts & 0x0000FF00) >> 8)
              << (uint8_t)(ts & 0x000000FF);
    tx_buffer << (uint8_t)((ssrc & 0xFF000000) >> 24)
              << (uint8_t)((ssrc & 0x00FF0000) >> 16)
              << (uint8_t)((ssrc & 0x0000FF00) >> 8)
              << (uint8_t)(ssrc & 0x000000FF);
    for (int i = 0; i < 160; i++) {
      tx_buffer << transmitBuffer[i];
    }

    socket->send_buffered_data();

    TXbufferRendered = false;
  }
}

// RTPpacket: od [12] - payload
void audioRX(std::string RTPpacket) {
  i2s_resume();

  rtpBuffor media;
  uint8_t tstamp[4] = {RTPpacket[7], RTPpacket[6], RTPpacket[5], RTPpacket[4]};
  media.timestamp = *((uint32_t *)tstamp);

  if ((int)RTPpacket[1] == 0) {
    // ESP_LOGI("audio_client", "codec: PCMU");
    audio_codec = 0;
    int j = 0;
    for (int i = 12; i < RTPpacket.size(); i++) {
      media.payload[j] = chcopy(ulaw2linear(RTPpacket[i]));
      j++;
    }
    RXaudioBuffer.push_front(media);
    RXbufferRendered = true;
  } else if ((int)RTPpacket[1] == 8) {
    // ESP_LOGI("audio_client", "codec: PCMA");
    audio_codec = 8;
    int j = 0;
    for (int i = 12; i < RTPpacket.size(); i++) {
      media.payload[j] = chcopy(alaw2linear(RTPpacket[i]));
      j++;
    }
    RXaudioBuffer.push_front(media);
    RXbufferRendered = true;
  }

  if (RXbufferRendered && i2sInitialized) {
    if (RXaudioBuffer.size() >= VOIP_BUFFER_LEN) {
      std::sort(std::begin(RXaudioBuffer), std::end(RXaudioBuffer));

      size_t bytes_written = 0;
      i2s_write(SPK, (const void *)&RXaudioBuffer.front().payload,
                sizeof(RXaudioBuffer.front().payload), &bytes_written,
                max_wait_spk);

      RXaudioBuffer.pop_front();
    }
    RXbufferRendered = false;
  }
}

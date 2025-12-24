#include "web_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

// Небольшой, предсказуемый буфер: последние N строк (без heap).
static constexpr uint32_t WEB_LOG_LINES = 200;
static constexpr uint32_t WEB_LOG_LINE_MAX = 192;

typedef int (*vprintf_like_t)(const char *fmt, va_list ap);

typedef struct {
  uint32_t seq;
  uint16_t len;
  char data[WEB_LOG_LINE_MAX];
} web_log_line_t;

static web_log_line_t s_lines[WEB_LOG_LINES];
static volatile uint32_t s_seq = 0;
static volatile uint32_t s_head = 0;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static vprintf_like_t s_prev_vprintf = nullptr;
static bool s_inited = false;

static inline void push_line(const char *s) {
  if (!s || s[0] == '\0') return;

  // Уберём завершающие \r/\n, чтобы хранить 1 строку = 1 запись.
  char tmp[WEB_LOG_LINE_MAX];
  size_t n = strnlen(s, sizeof(tmp) - 1);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
    n--;
  }
  if (n == 0) return;
  memcpy(tmp, s, n);
  tmp[n] = '\0';

  portENTER_CRITICAL(&s_mux);
  const uint32_t seq = ++s_seq;
  const uint32_t idx = (s_head++) % WEB_LOG_LINES;
  s_lines[idx].seq = seq;
  s_lines[idx].len = (uint16_t)n;
  memcpy(s_lines[idx].data, tmp, n + 1);
  portEXIT_CRITICAL(&s_mux);
}

static int web_vprintf(const char *fmt, va_list ap) {
  // Сначала печатаем "как обычно"
  int rc = 0;
  if (s_prev_vprintf && s_prev_vprintf != web_vprintf) {
    va_list ap_print;
    va_copy(ap_print, ap);
    rc = s_prev_vprintf(fmt, ap_print);
    va_end(ap_print);
  }

  // И отдельно складываем в буфер (копируем va_list!)
  va_list ap2;
  va_copy(ap2, ap);
  char buf[WEB_LOG_LINE_MAX];
  const int n = vsnprintf(buf, sizeof(buf), fmt, ap2);
  va_end(ap2);
  if (n > 0) {
    push_line(buf);
  }
  return rc;
}

void web_log_init(void) {
  if (s_inited) return;
  s_inited = true;

  // Подключаем перехват логов
  s_prev_vprintf = (vprintf_like_t)esp_log_set_vprintf(web_vprintf);

  // Отметим в логе (и оно же попадёт в буфер)
  ESP_LOGI("WEB_LOG", "web log capture enabled (last %u lines)", (unsigned)WEB_LOG_LINES);
}

uint32_t web_log_last_seq(void) { return (uint32_t)s_seq; }

uint32_t web_log_earliest_seq(void) {
  const uint32_t last = (uint32_t)s_seq;
  return (last >= WEB_LOG_LINES) ? (last - WEB_LOG_LINES + 1) : 1;
}

bool web_log_get_line(uint32_t seq, char *out, uint32_t out_len) {
  if (!out || out_len == 0 || seq == 0) return false;
  const uint32_t idx = (seq - 1) % WEB_LOG_LINES;
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  if (s_lines[idx].seq == seq) {
    const uint16_t n = s_lines[idx].len;
    const uint32_t copy_n = (n < (out_len - 1)) ? (uint32_t)n : (out_len - 1);
    memcpy(out, s_lines[idx].data, copy_n);
    out[copy_n] = '\0';
    ok = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}



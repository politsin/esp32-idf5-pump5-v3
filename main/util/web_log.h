#pragma once

#include <stdbool.h>
#include <stdint.h>

// Инициализирует перехват esp_log (через esp_log_set_vprintf) и буфер последних строк.
// Безопасно вызывать повторно.
void web_log_init(void);

// Возвращает текущий seq последней строки.
uint32_t web_log_last_seq(void);

// Самый старый seq, который ещё гарантированно может быть в буфере.
uint32_t web_log_earliest_seq(void);

// Получить строку по seq. Вернёт false, если уже перезаписано/не найдено.
bool web_log_get_line(uint32_t seq, char *out, uint32_t out_len);



#ifndef TELEGRAM_BOT_H_
#define TELEGRAM_BOT_H_
//-------------------------------------------------------------
#include <esp_err.h>
//-------------------------------------------------------------

// Telegram Bot API
#define TELEGRAM_BOT_TOKEN "TELEGRAM_BOT_TOKEN"
#define TELEGRAM_CHAT_ID   "TELEGRAM_CHAT_ID"

//-------------------------------------------------------------
esp_err_t telegram_send_message(const char *text);
//-------------------------------------------------------------
#endif /* TELEGRAM_BOT_H_ */

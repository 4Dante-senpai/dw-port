#ifndef DW1_WEB_CARD_H
#define DW1_WEB_CARD_H
void *web_card_address(int port);
void web_card_attach(int port, int blank);
void web_card_test(unsigned iterations);
#endif

#ifndef PORT_HOST_CARD_H
#define PORT_HOST_CARD_H
/* Invalid/unreadable paths report to stderr and leave the slot absent. */
int host_card_open(int port, const char *path);
void host_card_close(int port);
#endif

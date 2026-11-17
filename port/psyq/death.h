/* Death dump for the native playtest: state, recent inputs, overlay and raw frames of the game
 * thread, written to the --death-file descriptor when the process dies (docs/port/playtest.md). */
#ifndef PORT_DEATH_H
#define PORT_DEATH_H
#include <pthread.h>
#ifdef __EMSCRIPTEN__
static inline void port_death_dump(const char *reason, int sig, const void *addr) { (void)reason; (void)sig; (void)addr; }
static inline void port_death_game_thread(pthread_t thread) { (void)thread; }
#else
/* Installs the fault handlers; fd >= 0 also installs abort/quit and the sanitizer callback. */
void port_death_install(int fd);
/* Writes the dump once per process (the first caller wins); a no-op without a death file. */
void port_death_dump(const char *reason, int sig, const void *addr);
/* The thread whose backtrace SIGQUIT must report. */
void port_death_game_thread(pthread_t thread);
#endif
#endif

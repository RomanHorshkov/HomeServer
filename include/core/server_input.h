#ifndef SERVER_INPUT_H
#define SERVER_INPUT_H

/**
 * @file server_input.h
 * @brief Terminal input utility for server shutdown.
 *
 * Provides a non-blocking check for keyboard input on `stdin`, allowing
 * graceful shutdown when the user types `'q' + ENTER'` during server runtime.
 */

/**
 * @brief Check for user input on stdin and detect `'q'` command.
 *
 * This function sets `stdin` to non-blocking mode (only once),
 * then reads input from the terminal and checks if the letter `'q'`
 * was entered. If so, it logs the event and returns `0` to signal exit.
 *
 * @return 0 if `'q'` was detected, -1 otherwise
 */
int check_stdin_for_exit(void);

#endif /* SERVER_INPUT_H */
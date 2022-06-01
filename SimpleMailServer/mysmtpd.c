#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
#define TEMP_FILE_NAME  "mail.XXXXXX.tmp"
#define TERMINATE_DATA  ".\r\n"

#define WELCOME_MESSAGE "Simple Mail Transfer Service Ready"
#define OK_MESSAGE      "OK"
#define CLOSE_MESSAGE   "Service closing transmission channel"

#define HELO_GREET_MESSAGE          "greets"
#define HELO_INVALID_ARGS_MESSAGE   "expected a single argument with a domain identifier"

#define UNSUPPORTED_COMMAND_MESSAGE "is unsupported in this SMTP server"
#define INVALID_ARGS_MESSAGE        "Invalid arguments:"

#define VRFY_INVALID_ARGS_MESSAGE   "expected a username with a domain name only <user@domain.com>"

#define MAIL_INVALID_ARGS_MESSAGE   "expected a 'FROM:' and the sender's email address <FROM:<sender@domain.com>>"
#define MAIL_SENDER_EXISTS_MESSAGE  "Sender already exists! Must reset (RSET) to change it"
#define MAIL_NO_HELO_MESSAGE        "HELO/EHLO command was not called. Must call HELO/EHLO first"

#define RCPT_INVALID_ARGS_MESSAGE   "expected a 'TO:' and the recipient's email address <TO:<recipient@domain.com>>"
#define RCPT_USER_NOT_FOUND_MESSAGE "Recipient is not a registered user in this server"
#define RCPT_NO_SENDER_MESSAGE      "No sender is set. Must call MAIL first"

#define DATA_NO_RCPT_MESSAGE    "No recipient(s) set. Must call RCPT first"
#define DATA_READY_MESSAGE      "Ready to accept mail. End with '.' to end the message"
#define DATA_SUCCESS_MESSAGE    "Mail accepted for delivery"
#define DATA_FAILURE_MESSAGE    "Message denied"

#define USER_EXISTS_MESSAGE     "User found"
#define USER_NOT_FOUND_MESSAGE  "User not found"

#define INVALID_COMMAND_MESSAGE "is not a valid command"

#define HELO "helo"
#define EHLO "ehlo"
#define NOOP "noop"
#define QUIT "quit"
#define VRFY "vrfy"
#define MAIL "mail"
#define RCPT "rcpt"
#define DATA "data"
#define RSET "rset"
#define EXPN "expn"
#define HELP "help"

#define CODE_CONNECT    220
#define CODE_CLOSE      221
#define CODE_SUCCESS    250

#define CODE_START_DATA_INPUT   354

#define CODE_INVALID_COMMAND    500
#define CODE_INVALID_ARGS       501
#define CODE_COMMAND_NO_SUPPORT 502
#define CODE_BAD_SEQUENCE       503
#define CODE_GENERAL_FAILURE    550
#define CODE_USER_NOT_LOCAL     551
#define CODE_BAD_DATA_INPUT     554

#define FROM_PREFIX     "FROM:"
#define TO_PREFIX       "TO:"
#define ADDRESS_START   '<'
#define ADDRESS_END     '>'

static void handle_client(int fd);

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
        return 1;
    }

    run_server(argv[1], handle_client);

    return 0;
}

/* Counts the number of times a given character occurs in the string.
*
*  Parameters: character:   Character to look for.
*              str:         String to crawl through.
*
*  Returns: Number of times character appears in the string
*/
static int get_char_count(const char character, const char* str) {
    int count = 0;
    for (int i = 0; i < strlen(str); i++) {
        if (str[i] == character)
            count++;
    }
    return count;
}

/*  Takes a string to get the email address from a MAIL or RCPT command and
*   writes the isolated address to a buffer.
*
*
*  Parameters: str:     String to isolate.
*              strout:  String to write the isolated address to.
*
*  Returns: 1 if the string is successfully isolated into an email address
*           0 if the string does not contain '<' or '>' or both
*/
static int parse_email_address(const char* str, char* strout) {
    // Check to see if there is a '<' and '>' to extract an email from
    if (get_char_count(ADDRESS_START, str) != 1 || get_char_count(ADDRESS_END, str) != 1)
        return 0;

    char* address_start = strchr(str, ADDRESS_START);   // <user@domain.com>
    address_start += sizeof(char);                      // user@domain.com>
    char* address_end = strchr(str, ADDRESS_END);       // >
    size_t length = address_end - address_start;        // hacky pointer arithmetic bs
    if (length < 1)
        return 0;                                       // why tf did someone put the end before the start?
    strncpy(strout, address_start, length);             // user@domain.com
    strout[length] = NULL;                              // terminate string
    return 1;
}

/* Takes a string and sees if a string starts with the same sequence as prefix
*
*  Parameters: str:     String to be validated
*              prefix:  String to look for at the beginning of str
*
*  Returns: 1 if the str starts with prefix
*           0 otherwise
*/
static int contains_prefix(const char* str, const char* prefix) {
    return !strncasecmp(str, prefix, strlen(prefix));
}

/* Writes a file in a line-per-line basis from a connection.
*
*  Parameters: nb:          Net buffer to extract user input from
*              filename:    Name of the file to write to. Generates file if it does not exist
*
*  Returns: 1 if operation is successful
*           0 if an error has occurred and the transaction did not complete
*/
int handle_data(struct net_buffer* nb, const char* filename) {
    FILE* tempfile = fopen(filename, "a");          // Open the temp file, makes it if it does not exist for some reason
    int terminate_strlen = strlen(TERMINATE_DATA);  // Length of the terminating string, useful for later

    while (1) {
        char recvbuf[MAX_LINE_LENGTH + 1];
        char* sneakyPointerMath = recvbuf;  // For now, set this to point at the beginning of the string

        int connectionState = nb_read_line(nb, recvbuf);
        recvbuf[MAX_LINE_LENGTH] = NULL;    // Security reasons, the end of the array will always end with a NULL
        // in case it doesn't contain any

        // Connection interrupted, throw it out!
        if (connectionState <= 0) {
            fclose(tempfile);
            return 0;
        }

        // Case where line is exactly DATA_TERMINATE, ending the message
        if (!strncasecmp(recvbuf, TERMINATE_DATA, terminate_strlen)) {
            fclose(tempfile);
            return 1;
        }
        // Case where the stem DATA_TERMINATE string (without CRLF) is appended as an extra
        // due to deliberate user input instead of ending the message
        //
        // However, this case is catch-all to all lines that start with a single '.' which never happens
        // unless one is to make a DATA entry from scratch in netcat
        if (!strncasecmp(recvbuf, TERMINATE_DATA, terminate_strlen - 2)) // remove CRLF, raw string.
            sneakyPointerMath += terminate_strlen - 2;  // Remove the duplicate by advancing the pointer by
        // the length of the terminating string w/o CRLF
        // (in normal cases, this is "." so advance by 1)
        // I love pointer arithmetic LMAO

        fputs(sneakyPointerMath, tempfile);
    }
}

void handle_client(int fd) {
    struct utsname my_uname;
    struct user_list* users = create_user_list();
    uname(&my_uname);

    int has_helo = 0;       // Did the client call HELO/EHLO at least once?
    int has_sender = 0;     // Is sender present?
    int has_recipient = 0;  // Is there a recipient?

    // Welcome message
    send_formatted(fd, "%d %s %s\r\n", CODE_CONNECT, my_uname.__domainname, WELCOME_MESSAGE);

    while (1) {
        char recvbuf[MAX_LINE_LENGTH + 1];
        net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);
        recvbuf[MAX_LINE_LENGTH] = NULL;    // Security reasons, the end of the array will always end with a NULL
        // in case it doesn't contain any

        /* TO BE COMPLETED BY THE STUDENT */

        int connectionState = nb_read_line(nb, recvbuf);
        // Connection interrupted, throw it out!
        if (connectionState <= 0) {
            dlog("server: Connection interrupted. Aborting connection fd: %d", fd);
            nb_destroy(nb);
            break;
        }

        char raw_recvbuf[MAX_LINE_LENGTH + 1];    // raw buffer before being split; useful for debugging
        strcpy(raw_recvbuf, recvbuf);
        int argcount = get_char_count(' ', recvbuf);
        char* line[argcount + 1];
        split(recvbuf, line);

        // Empty line, ignore!
        if (line[0] == NULL) {
            nb_destroy(nb);
            continue;
        }

        // HELO/EHLO
        if (!strcasecmp(line[0], EHLO) || !strcasecmp(line[0], HELO)) {
            // Bad arguments and syntax
            if (argcount != 1 || line[1] == NULL) {
                dlog("server: received EHLO/HELO command but failed due to bad arguments. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s %s\r\n", CODE_INVALID_ARGS, INVALID_ARGS_MESSAGE, HELO_INVALID_ARGS_MESSAGE);
                continue;
            }
            dlog("server: received HELO/EHLO command. Line: %s", raw_recvbuf);
            has_helo++;
            send_formatted(fd, "%d %s %s %s\r\n", CODE_SUCCESS, my_uname.nodename, HELO_GREET_MESSAGE, line[1]);
        }

            // NOOP
        else if (!strcasecmp(line[0], NOOP)) {
            dlog("server: received NOOP command. Line: %s", raw_recvbuf);
            send_formatted(fd, "%d %s\r\n", CODE_SUCCESS, OK_MESSAGE);
        }

            // QUIT
        else if (!strcasecmp(line[0], QUIT)) {
            dlog("server: received QUIT command. Line: %s", raw_recvbuf);
            send_formatted(fd, "%d %s %s\r\n", CODE_CLOSE, my_uname.__domainname, CLOSE_MESSAGE);
            nb_destroy(nb);
            break;
        }

            // VRFY
        else if (!strcasecmp(line[0], VRFY)) {
            if (argcount != 1 || line[1] == NULL) {
                // Case where there are too many or too little args or
                // a null username
                dlog("server: received VRFY command but failed to bad arguments. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s %s\r\n", CODE_INVALID_ARGS, INVALID_ARGS_MESSAGE, VRFY_INVALID_ARGS_MESSAGE);
                continue;
            }

            dlog("server: received VRFY command. Line: %s", raw_recvbuf);
            // Defaults to the user not being found
            int code = CODE_GENERAL_FAILURE;
            char* msg = USER_NOT_FOUND_MESSAGE;
            // If user is found, switch
            if (is_valid_user(line[1], NULL)) {
                code = CODE_SUCCESS;
                msg = USER_EXISTS_MESSAGE;
            }
            send_formatted(fd, "%d %s\r\n", code, msg);
        }

            // MAIL
        else if (!strcasecmp(line[0], MAIL)) {
            // Bad arguments and syntax
            if (argcount != 1 || line[1] == NULL || !contains_prefix(line[1], FROM_PREFIX)) {
                dlog("server: received MAIL command but failed due to bad arguments. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s %s\r\n", CODE_INVALID_ARGS, INVALID_ARGS_MESSAGE, MAIL_INVALID_ARGS_MESSAGE);
                continue;
            }
            // Sender already exists and there can be no more than 1 sender
            if (has_sender) {
                dlog("server: received MAIL command but sender already exists. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s\r\n", CODE_BAD_SEQUENCE, MAIL_SENDER_EXISTS_MESSAGE);
                continue;
            }
            // Client did not HELO/EHLO
            if (!has_helo) {
                dlog("server: received MAIL command but HELO/EHLO was not called. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s\r\n", CODE_BAD_SEQUENCE, MAIL_NO_HELO_MESSAGE);
                continue;
            }
            char address[strlen(line[1])];
            // Bad email address format, cannot extract the address
            if (!parse_email_address(line[1], address)) {
                dlog("server: received MAIL command but failed due to bad syntax. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s %s\r\n", CODE_INVALID_ARGS, INVALID_ARGS_MESSAGE, MAIL_INVALID_ARGS_MESSAGE);
                continue;
            }

            dlog("server: received MAIL command. Line: %s", raw_recvbuf);

            has_sender++;
            send_formatted(fd, "%d %s\r\n", CODE_SUCCESS, OK_MESSAGE);
        }

            // RCPT
        else if (!strcasecmp(line[0], RCPT)) {
            // Bad arguments and syntax
            if (argcount != 1 || line[1] == NULL || !contains_prefix(line[1], TO_PREFIX)) {
                dlog("server: received RCPT command but failed due to bad arguments. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s %s\r\n", CODE_INVALID_ARGS, INVALID_ARGS_MESSAGE, RCPT_INVALID_ARGS_MESSAGE);
                continue;
            }
            // No sender set, MAIL was not called prior
            if (!has_sender) {
                dlog("server: received RCPT command but no sender address was provided. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s\r\n", CODE_BAD_SEQUENCE, RCPT_NO_SENDER_MESSAGE);
                continue;
            }

            char address[strlen(line[1])];
            // Bad email address format, cannot extract the address
            if (!parse_email_address(line[1], address)) {
                dlog("server: received RCPT command but failed due to bad syntax. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s %s\r\n", CODE_INVALID_ARGS, INVALID_ARGS_MESSAGE, RCPT_INVALID_ARGS_MESSAGE);
                continue;
            }
            // This user is not local
            if (!is_valid_user(address, NULL)) {
                dlog("server: received RCPT command but user does not exist. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s\r\n", CODE_USER_NOT_LOCAL, RCPT_USER_NOT_FOUND_MESSAGE);
                continue;
            }

            dlog("server: received RCPT command. Line: %s", raw_recvbuf);
            add_user_to_list(&users, address);
            has_recipient++;
            send_formatted(fd, "%d %s\r\n", CODE_SUCCESS, OK_MESSAGE);
        }

            // DATA
        else if (!strcasecmp(line[0], DATA)) {
            // No recipient, throw it out!
            if (!has_recipient) {
                dlog("server: received DATA command but no sender address was provided. Line: %s", raw_recvbuf);
                send_formatted(fd, "%d %s\r\n", CODE_BAD_SEQUENCE, DATA_NO_RCPT_MESSAGE);
                continue;
            }
            dlog("server: received DATA command. Line: %s", raw_recvbuf);
            send_formatted(fd, "%d %s\r\n", CODE_START_DATA_INPUT, DATA_READY_MESSAGE);

            char tempfile[] = TEMP_FILE_NAME;
            mkstemp(tempfile);

            int state = handle_data(nb, tempfile);

            // Success, mail sent with no problems
            if (state) {
                save_user_mail(tempfile, users);
                unlink(tempfile);
                dlog("server: DATA command finished. Filename: %s", tempfile);
                send_formatted(fd, "%d %s\r\n", CODE_SUCCESS, DATA_SUCCESS_MESSAGE);
            }
                // Connection interrupted and the nb is probably garbage now too
                // destroy and terminate connection!!
            else {
                unlink(tempfile);
                dlog("server: DATA command interrupted due to a network issue. Aborting connection fd: %d", fd);
                nb_destroy(nb);
                break;
            }

        }
            // RSET
        else if (!strcasecmp(line[0], RSET)) {
            has_sender = 0;
            has_recipient = 0;

            destroy_user_list(users);
            users = create_user_list();

            dlog("server: received RSET command. Line: %s", raw_recvbuf);
            send_formatted(fd, "%d %s\r\n", CODE_SUCCESS, OK_MESSAGE);
        }

            // Unsupported commands EXPN and HELP
        else if (!strcasecmp(line[0], EXPN) || !strcasecmp(line[0], HELP)) {
            dlog("server: received unsupported command. Line: %s", raw_recvbuf);
            send_formatted(fd, "%d \"%s\" %s\r\n", CODE_COMMAND_NO_SUPPORT, line[0], UNSUPPORTED_COMMAND_MESSAGE);
        }

            // Everything else
        else {
            dlog("server: received unknown command. Line: %s", raw_recvbuf);
            send_formatted(fd, "%d \"%s\" %s\r\n", CODE_INVALID_COMMAND, line[0], INVALID_COMMAND_MESSAGE);
        }

        nb_destroy(nb);
    }

    destroy_user_list(users);
}
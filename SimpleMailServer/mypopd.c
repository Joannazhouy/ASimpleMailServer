#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/utsname.h>

#define MAX_LINE_LENGTH 1024
#define TERMINATE_DATA	".\r\n"

#define GREETING_MESSAGE    "POP3 server ready"

#define USER_FOUND_MESSAGE		"Welcome,"
#define USER_NOT_FOUND_MESSAGE	"No mailbox is found for"

#define PASS_CORRECT_MESSAGE			"User authenticated."
#define PASS_INCORRECT_MESSAGE			"Wrong password, try again."
#define PASS_USER_UNDEFINED_MESSAGE		"Must call USER first."

#define DELE_SUCCESS_MESSAGE	"Message deleted"
#define DELE_INVALID_MESSAGE	"Invalid message"
#define DELE_NOT_FOUND_MESSAGE	"Item does not exist"

#define RETR_SUCCESS_MESSAGE	"Displaying mail contents:"
#define RETR_INVALID_MESSAGE	"Invalid message name. Message name should be a number"
#define RETR_NOT_FOUND_MESSAGE	"Item does not exist"
#define RETR_OCTETS_MESSAGE		"octets"


#define RSET_RESTORED_MESSAGE	"messages restored"

#define BAD_FORMAT_MESSAGE			"Bad format for the command."
#define NOT_AUTHENTICATED_MESSAGE	"Log in first."
#define UNKNOWN_COMMAND_MESSAGE		"Unknown command. Try again"

// Commands
#define PASS "pass"
#define USER "user"
#define NOOP "noop"
#define QUIT "quit"
#define STAT "stat"
#define LIST "list"
#define RETR "retr"
#define DELE "dele"
#define RSET "rset"

// Codes
#define OK  "+OK"
#define ERR "-ERR"

static void handle_client(int fd);

int main(int argc, char* argv[]) {

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

/* Checks if the string only contains numeric characters
*
*  Parameters: str:		String to check if it is convertible into a valid integer.
*
*  Returns: 1 if the string only contains numeric characters [0-9]
*			0 if the string contains any other type of character
*/
static int is_valid_int(const char* str) {
	for (int i = 0; i < strlen(str); i++) {
		if (!isdigit(str[i]))
			return 0;
	}
	return 1;
}


/* Reads the given mail and displays each line in a net buffer
*  CONSTRAINTS: assumes that a live connection has been established (net_buffer)
*				!! assumes that mail is valid and NOT NULL !!
*				Every line is (length < MAX_LINE_LENGTH)
*
*  Parameters:	fd:		File descriptor used by the net buffer to communicae with the client
*				mail:	Source mail item to read the data from
*
*/
void display_mail(const int fd, const struct mail_item* mail) {
	FILE* file = get_mail_item_contents(mail);
	char line[MAX_LINE_LENGTH];

	while (fgets(line, MAX_LINE_LENGTH, file)) {
		if (line[0] == TERMINATE_DATA[0])
			send_formatted(fd, "%c%s", TERMINATE_DATA[0], line);
		else
			send_formatted(fd, "%s", line);
	}
	send_formatted(fd, "%s", TERMINATE_DATA);
	fclose(file);
}

void handle_client(int fd) {
	struct utsname my_uname;
	uname(&my_uname);
	char recvbuf[MAX_LINE_LENGTH + 1];
	net_buffer_t nb = nb_create(fd, MAX_LINE_LENGTH);

	/* TO BE COMPLETED BY THE STUDENT */
	struct mail_list* m_list = NULL;
	unsigned int mail_count = 0;

	char* username = NULL;
	int authenticated = 0;

	//send the greeting message
	send_formatted(fd, "%s %s\r\n", OK, GREETING_MESSAGE);
	while (1) {
		int connectionState = nb_read_line(nb, recvbuf);
		// Connection interrupted, throw it out!
		if (connectionState <= 0) {
			dlog("server: Connection interrupted. Aborting connection fd: %d", fd);
			break;
		}
		recvbuf[MAX_LINE_LENGTH] = NULL;    // Security reasons, the end of the string will always end with a NULL 
											// in case the user input doesn't contain any (very bad thing!!)

		int argcount = get_char_count(' ', recvbuf);
		char* line[argcount + 1];
		split(recvbuf, line);

		// Empty response, ignore.
		if (line[0] == NULL) {
			continue;
		}

		//USER
		if (!strcasecmp(line[0], USER)) {

			dlog("server: received USER command\n");
			if (argcount != 1 || !line[1]) {
				send_formatted(fd, "%s %s\r\n", ERR, BAD_FORMAT_MESSAGE);
				continue;
			}
			char* message = USER_NOT_FOUND_MESSAGE;
			char* code = ERR;
			if (is_valid_user(line[1], NULL)) {
				message = USER_FOUND_MESSAGE;
				code = OK;
				username = strdup(line[1]);
			}
			send_formatted(fd, "%s %s %s\r\n", code, message, line[1]);

		}

		//PASS
		else if (!strcasecmp(line[0], PASS)) {

			dlog("server: received PASS command\n");
			if (argcount != 1 || !line[1]) {
				send_formatted(fd, "%s %s\r\n", ERR, BAD_FORMAT_MESSAGE);
				continue;
			}
			if (!username) {
				send_formatted(fd, "%s %s\r\n", ERR, PASS_USER_UNDEFINED_MESSAGE);
				continue;
			}
			char* message = PASS_INCORRECT_MESSAGE;
			char* code = ERR;
			if (is_valid_user(username, line[1])) {
				message = PASS_CORRECT_MESSAGE;
				code = OK;
				authenticated++;
				m_list = load_user_mail(username);
			}
			send_formatted(fd, "%s %s\r\n", code, message);
		}

		//do easy NOOP first
		else if (!strcasecmp(line[0], NOOP)) {
			dlog("server: received NOOP command\n");
			if (!authenticated) {
				send_formatted(fd, "%s %s\r\n", ERR, NOT_AUTHENTICATED_MESSAGE);
			} else
                send_formatted(fd, "%s\r\n", OK);
		}
		//QUIT
		else if (!strcasecmp(line[0], QUIT)) {
			dlog("server: received QUIT command\n");
			send_formatted(fd, "%s %s\r\n", OK, QUIT);
			break;
		}
		//STAT
		else if (!strcasecmp(line[0], STAT)) {
			dlog("server: received STAT command\n");
			if (!authenticated) {
				send_formatted(fd, "%s %s\r\n", ERR, NOT_AUTHENTICATED_MESSAGE);
			} else if (m_list != NULL){
                   mail_count = get_mail_count(m_list, 0);
                   int list_size = (int)get_mail_list_size(m_list);
                   send_formatted(fd, "+OK %d %d\r\n", mail_count, list_size);
            } else {
                //mail list is empty
                mail_count = 0;
                send_formatted(fd, "+OK 0 0\r\n");
            }
          }

		//LIST
		else if (!strcasecmp(line[0], LIST)) {

            dlog("server: received LIST command\n");
            if (!authenticated) {
                send_formatted(fd, "%s %s\r\n", ERR, NOT_AUTHENTICATED_MESSAGE);
                continue;
            }
            if (!line[1]){
                int mail_size = (int)get_mail_list_size(m_list);
                unsigned int mc = get_mail_count(m_list, 0);
                unsigned int mail_count_del = get_mail_count(m_list, 1);
                if (mc == 0){
                    send_formatted(fd, "+OK 0 0\r\n");
                    send_formatted(fd, ".\r\n");
                } else {
                    send_formatted(fd, "+OK %d messages, (%d octets)\r\n", mc, mail_size);
                    for (int i = 0; i < mail_count_del; ++i) {
                        struct mail_item* mailItem = get_mail_item(m_list, i);
                        if (mailItem != NULL) {
                            int size =(int) get_mail_item_size(mailItem);
                            send_formatted(fd, "%d %d\r\n", i+1, size);
                        }
                    }
                    send_formatted(fd, ".\r\n");
                }
            }
            else{
                if (atoi(line[1] == 0) ){
                    send_formatted(fd, "-ERR msg does not exist\r\n");
                } else {
                    struct mail_item *mailItem = get_mail_item(m_list, atoi(line[1]) - 1);
                    if (mailItem != NULL) {
                        int size = (int) get_mail_item_size(mailItem);
                        send_formatted(fd, "+OK %d %d\r\n", atoi(line[1]), size);
                    } else {
                        send_formatted(fd, "-ERR msg does not exist");
                    }
                }

            }
		}
		//RETR
		else if (!strcasecmp(line[0], RETR)) {
			dlog("server: received RETR command\n");
			if (!authenticated) {
				send_formatted(fd, "%s %s\r\n", ERR, NOT_AUTHENTICATED_MESSAGE);
				continue;
			}
			if (argcount != 1 || !line[1]) {
				send_formatted(fd, "%s %s\r\n", ERR, BAD_FORMAT_MESSAGE);
				continue;
			}
			if (!is_valid_int(line[1])) {
				send_formatted(fd, "%s %s\r\n", ERR, RETR_INVALID_MESSAGE);
				continue;
			}

			int num_msg = atoi(line[1]);
			struct mail_item* mail = get_mail_item(m_list, num_msg - 1);
			if (!mail) {
				send_formatted(fd, "%s %s\r\n", ERR, RETR_NOT_FOUND_MESSAGE);
				continue;
			}
			send_formatted(fd, "%s %d %s\r\n", OK, get_mail_item_size(mail), RETR_OCTETS_MESSAGE);
			display_mail(fd, mail);
		}
		//DELE
		else if (!strcasecmp(line[0], DELE)) {
			dlog("server: received DELE command\n");
            struct mail_item* mail;
            if (!authenticated) {
				send_formatted(fd, "%s %s\r\n", ERR, NOT_AUTHENTICATED_MESSAGE);
				continue;
			}
			if (argcount != 1 || !line[1]) {
				send_formatted(fd, "%s %s\r\n", ERR, BAD_FORMAT_MESSAGE);
				continue;
			}
			//checking msg validity
			if (!is_valid_int(line[1])) {
				send_formatted(fd, "%s %s\r\n", ERR, DELE_INVALID_MESSAGE);
				continue;
			}
			int num_msg = atoi(line[1]);

			mail = get_mail_item(m_list, num_msg - 1);
			if (mail != NULL) {
				mark_mail_item_deleted(mail);
				//mail_count = get_mail_count(m_list, 0);
				send_formatted(fd, "%s %s\r\n", OK, DELE_SUCCESS_MESSAGE);
			}
			else {
				send_formatted(fd, "%s %s\r\n", ERR, DELE_NOT_FOUND_MESSAGE);
			}
		}
		//RSET
		else if (!strcasecmp(line[0], RSET)) {

			dlog("server: received RSET command\n");
			if (!authenticated) {
				send_formatted(fd, "%s %s\r\n", ERR, NOT_AUTHENTICATED_MESSAGE);
				continue;
			} else {
                unsigned int count = reset_mail_list_deleted_flag(m_list);
                send_formatted(fd, "%s %d %s\r\n", OK, count, RSET_RESTORED_MESSAGE);
            }
		}
		// Unknown commands
		else {
			dlog("server: received unknown command\n");
			send_formatted(fd, "%s %s\r\n", ERR, UNKNOWN_COMMAND_MESSAGE);
		}
	}
	nb_destroy(nb);
	destroy_mail_list(m_list);
	if (username)
		free(username);
}
